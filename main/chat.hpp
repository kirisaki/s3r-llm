#pragma once

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
// Appends a message and updates the screen. User messages get a "> " prefix.
void add(Speaker speaker, const char *text);

} // namespace chat
