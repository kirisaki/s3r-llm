#pragma once

#include <cstddef>

// Text I/O over the USB Serial/JTAG port (the USB-C connector).
namespace serial
{

void init();
// Blocks until a line has been entered and returns its length. Input is
// echoed, backspace works, and anything but printable ASCII is dropped.
size_t read_line(char *buf, size_t size);
// Dropped if no host is listening.
void write(const char *text);

} // namespace serial
