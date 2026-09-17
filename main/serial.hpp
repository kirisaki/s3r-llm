#pragma once

#include <cstddef>

// Text I/O over the USB Serial/JTAG port (the USB-C connector).
namespace serial
{

void init();
// Collects input for up to timeout_ms and returns true once a line is complete;
// until then buf holds the line so far and has to be passed in again. Input is
// echoed, backspace works, and anything but printable ASCII is dropped.
bool poll_line(char *buf, size_t size, int timeout_ms);
// Whether poll_line() is in the middle of a line.
bool typing();
// Dropped if no host is listening.
void write(const char *text);

} // namespace serial
