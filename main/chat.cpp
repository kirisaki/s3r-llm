#include "chat.hpp"

#include <cstring>

#include "display.hpp"
#include "font5x7.hpp"

namespace chat
{

namespace
{

constexpr int LINE_GAP = 2;
constexpr int SPEAKER_GAP = 3;
constexpr uint16_t USER_COLOR = display::rgb565(0, 200, 255);
constexpr uint16_t ASSISTANT_COLOR = display::WHITE;
constexpr char USER_PREFIX[] = "> ";

// Top of the line being written
int cursor_y = 0;
bool empty = true;
uint16_t color = ASSISTANT_COLOR;

// The line being written. Text arrives in pieces, so a word that turns out
// not to fit has to be taken back off the screen and moved to the next line.
char line[display::COLS];
int line_len = 0;
// Characters before this belong to the prefix and are not a place to wrap
int line_start = 0;
// The line is full and the space after it has been dropped; the break itself
// waits for the next character so that a message never ends in a blank line
bool break_pending = false;

void draw(int col, char c)
{
    // Scroll only once something is drawn, so that trailing blank lines cost
    // no screen space
    const int overflow = cursor_y + font5x7::HEIGHT - display::HEIGHT;
    if (overflow > 0) {
        display::scroll_up(overflow);
        cursor_y -= overflow;
    }
    display::draw_char(col * display::CELL_W, cursor_y, c, color);
}

void put(char c)
{
    line[line_len] = c;
    draw(line_len, c);
    line_len++;
}

void new_line()
{
    cursor_y += font5x7::HEIGHT + LINE_GAP;
    line_len = 0;
    line_start = 0;
    break_pending = false;
}

// Called with a full line and one more character to place. Breaks at the last
// space if there is one, otherwise in the middle of the word.
void wrap(char c)
{
    if (break_pending) {
        new_line();
        put(c);
        return;
    }
    if (c == ' ') {
        // The break takes the place of the space
        break_pending = true;
        return;
    }

    int last_space = line_len - 1;
    while (last_space > line_start && line[last_space] != ' ') {
        last_space--;
    }
    if (last_space <= line_start) {
        new_line();
        put(c);
        return;
    }

    char word[display::COLS];
    const int word_len = line_len - (last_space + 1);
    memcpy(word, line + last_space + 1, word_len);
    for (int col = last_space + 1; col < line_len; col++) {
        draw(col, ' ');
    }
    new_line();
    for (int i = 0; i < word_len; i++) {
        put(word[i]);
    }
    put(c);
}

} // namespace

void init()
{
    cursor_y = 0;
    empty = true;
    line_len = 0;
    line_start = 0;
    break_pending = false;
    display::clear();
    display::flush();
}

void begin(Speaker speaker)
{
    if (!empty) {
        new_line();
        cursor_y += SPEAKER_GAP - LINE_GAP;
    }
    empty = false;

    const bool user = speaker == Speaker::User;
    color = user ? USER_COLOR : ASSISTANT_COLOR;
    if (user) {
        for (const char *p = USER_PREFIX; *p; p++) {
            put(*p);
        }
        line_start = line_len;
    }
    display::flush();
}

void append(const char *text)
{
    for (; *text; text++) {
        if (*text == '\n') {
            if (break_pending) {
                new_line();
            }
            new_line();
        } else if (line_len < display::COLS) {
            put(*text);
        } else {
            wrap(*text);
        }
    }
    display::flush();
}

void add(Speaker speaker, const char *text)
{
    begin(speaker);
    append(text);
}

} // namespace chat
