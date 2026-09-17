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

// Top of the next line to be drawn
int cursor_y = 0;
bool empty = true;

// Draws one line at the cursor, scrolling first if it would not fit.
void put_line(const char *prefix, const char *text, int len, uint16_t color)
{
    const int overflow = cursor_y + font5x7::HEIGHT - display::HEIGHT;
    if (overflow > 0) {
        display::scroll_up(overflow);
        cursor_y -= overflow;
    }
    int x = 0;
    for (; *prefix; prefix++, x += display::CELL_W) {
        display::draw_char(x, cursor_y, *prefix, color);
    }
    for (int i = 0; i < len; i++, x += display::CELL_W) {
        display::draw_char(x, cursor_y, text[i], color);
    }
    cursor_y += font5x7::HEIGHT + LINE_GAP;
}

// Length of the first line of text when wrapped at cols. Breaks at the last
// space if there is one, otherwise in the middle of the word.
int wrap(const char *text, int cols)
{
    int last_space = -1;
    for (int i = 0; i <= cols; i++) {
        if (text[i] == '\0' || text[i] == '\n') {
            return i;
        }
        if (text[i] == ' ') {
            last_space = i;
        }
    }
    return last_space > 0 ? last_space : cols;
}

// The prefix goes in front of the first line only.
void put_text(const char *prefix, const char *text, uint16_t color)
{
    do {
        const int len = wrap(text, display::COLS - strlen(prefix));
        put_line(prefix, text, len, color);
        prefix = "";
        text += len;
        // The break itself takes the place of one space or newline
        if (*text == ' ' || *text == '\n') {
            text++;
        }
    } while (*text);
}

} // namespace

void init()
{
    cursor_y = 0;
    empty = true;
    display::clear();
    display::flush();
}

void add(Speaker speaker, const char *text)
{
    if (!empty) {
        cursor_y += SPEAKER_GAP - LINE_GAP;
    }
    empty = false;

    if (speaker == Speaker::User) {
        put_text(USER_PREFIX, text, USER_COLOR);
    } else {
        put_text("", text, ASSISTANT_COLOR);
    }
    display::flush();
}

} // namespace chat
