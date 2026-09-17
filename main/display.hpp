#pragma once

#include <cstdint>

// AtomS3R built-in LCD: 128x128 ST7735S on SPI, backlight on an LP5562 (I2C).
namespace display
{

constexpr int WIDTH = 128;
constexpr int HEIGHT = 128;

// 5x7 glyphs with 1px spacing
constexpr int CELL_W = 6;
constexpr int CELL_H = 8;
constexpr int COLS = WIDTH / CELL_W;
constexpr int ROWS = HEIGHT / CELL_H;

constexpr uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b)
{
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

constexpr uint16_t BLACK = rgb565(0, 0, 0);
constexpr uint16_t WHITE = rgb565(255, 255, 255);

void init();
void set_brightness(uint8_t brightness);

// Drawing goes to a framebuffer; nothing reaches the panel until flush().
void clear(uint16_t color = BLACK);
void draw_char(int x, int y, char c, uint16_t fg = WHITE, uint16_t bg = BLACK);
// x, y are in pixels. Wraps at the right edge and on '\n'.
void draw_text(int x, int y, const char *text, uint16_t fg = WHITE, uint16_t bg = BLACK);
void flush();

} // namespace display
