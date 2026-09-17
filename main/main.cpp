#include <cstdio>

#include "display.hpp"

extern "C" void app_main(void)
{
    display::init();
    display::draw_text(0, 0, "Hello, world");
    display::flush();
}
