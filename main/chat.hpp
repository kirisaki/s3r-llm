#pragma once

#include <cstdint>

// Chat log on top of the display: messages are appended at the bottom and the
// log scrolls up once it reaches the lower edge.
namespace chat
{

enum class Speaker
{
    User,
    Assistant,
};

// Clears the log. display::init() must have been called.
void init();
// Starts a new message. User messages get a "> " prefix.
void begin(Speaker speaker);
// Appends a piece of text to the current message and updates the screen.
// Pieces can be cut anywhere; wrapping comes out the same as for whole text.
void append(const char *text);
// Same, in a color of its own instead of the speaker's
void append(const char *text, uint16_t text_color);
// begin() and append() in one go
void add(Speaker speaker, const char *text);

} // namespace chat
