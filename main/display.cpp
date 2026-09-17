#include "display.hpp"

#include <cassert>

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/spi_master.h"
#include "esp_heap_caps.h"
#include "esp_lcd_io_spi.h"
#include "esp_lcd_panel_io.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "font5x7.hpp"

namespace display
{

namespace
{

// LCD (ST7735S). Some AtomS3R batches ship with a GC9107 instead, which
// needs a different init sequence and offsets; that one is not supported.
constexpr gpio_num_t PIN_MOSI = GPIO_NUM_21;
constexpr gpio_num_t PIN_SCLK = GPIO_NUM_15;
constexpr gpio_num_t PIN_CS = GPIO_NUM_14;
constexpr gpio_num_t PIN_DC = GPIO_NUM_42;
constexpr gpio_num_t PIN_RST = GPIO_NUM_48;
constexpr spi_host_device_t LCD_HOST = SPI2_HOST;
constexpr unsigned int LCD_CLOCK_HZ = 40 * 1000 * 1000;
// Position of the 128x128 glass inside the controller's 132x132 GRAM window
constexpr int OFFSET_X = 2;
constexpr int OFFSET_Y = 3;

// Backlight (LP5562)
constexpr gpio_num_t PIN_SDA = GPIO_NUM_45;
constexpr gpio_num_t PIN_SCL = GPIO_NUM_0;
constexpr uint16_t LP5562_ADDR = 0x30;
constexpr uint8_t LP5562_REG_ENABLE = 0x00;
constexpr uint8_t LP5562_REG_CONFIG = 0x08;
constexpr uint8_t LP5562_REG_W_PWM = 0x0E;
constexpr uint8_t LP5562_REG_LED_MAP = 0x70;

constexpr size_t FB_BYTES = WIDTH * HEIGHT * sizeof(uint16_t);

esp_lcd_panel_io_handle_t io = nullptr;
i2c_master_dev_handle_t backlight = nullptr;
// RGB565 in wire (big-endian) byte order
uint16_t *fb = nullptr;

constexpr uint16_t to_wire(uint16_t color)
{
    return (color >> 8) | (color << 8);
}

struct InitCmd
{
    uint8_t cmd;
    uint8_t len;
    uint8_t data[16];
    uint16_t delay_ms;
};

constexpr InitCmd INIT_CMDS[] = {
    {0x01, 0, {}, 150},                                   // software reset
    {0x11, 0, {}, 500},                                   // sleep out
    {0xB1, 3, {0x01, 0x2C, 0x2D}, 0},                     // frame rate, normal mode
    {0xB2, 3, {0x01, 0x2C, 0x2D}, 0},                     // frame rate, idle mode
    {0xB3, 6, {0x01, 0x2C, 0x2D, 0x01, 0x2C, 0x2D}, 0},   // frame rate, partial mode
    {0xB4, 1, {0x07}, 0},                                 // display inversion control
    {0xC0, 3, {0xA2, 0x02, 0x84}, 0},                     // power control 1..5
    {0xC1, 1, {0xC5}, 0},
    {0xC2, 2, {0x0A, 0x00}, 0},
    {0xC3, 2, {0x8A, 0x2A}, 0},
    {0xC4, 2, {0x8A, 0xEE}, 0},
    {0xC5, 1, {0x0E}, 0},                                 // VCOM
    {0xE0, 16, {0x02, 0x1C, 0x07, 0x12, 0x37, 0x32, 0x29, 0x2D, 0x29, 0x25, 0x2B, 0x39, 0x00, 0x01, 0x03, 0x10}, 0}, // gamma +
    {0xE1, 16, {0x03, 0x1D, 0x07, 0x06, 0x2E, 0x2C, 0x29, 0x2D, 0x2E, 0x2E, 0x37, 0x3F, 0x00, 0x00, 0x02, 0x10}, 0}, // gamma -
    {0x3A, 1, {0x05}, 0},                                 // 16 bit/pixel
    {0x36, 1, {0xC8}, 0},                                 // mounted upside down, BGR
    {0x21, 0, {}, 0},                                     // inversion on
    {0x13, 0, {}, 10},                                    // normal display mode
};

void lp5562_write(uint8_t reg, uint8_t value)
{
    const uint8_t buf[] = {reg, value};
    ESP_ERROR_CHECK(i2c_master_transmit(backlight, buf, sizeof(buf), 100));
}

void init_backlight()
{
    i2c_master_bus_config_t bus_config = {};
    bus_config.i2c_port = -1;
    bus_config.sda_io_num = PIN_SDA;
    bus_config.scl_io_num = PIN_SCL;
    bus_config.clk_source = I2C_CLK_SRC_DEFAULT;
    bus_config.glitch_ignore_cnt = 7;
    bus_config.flags.enable_internal_pullup = true;
    i2c_master_bus_handle_t bus = nullptr;
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_config, &bus));

    i2c_device_config_t dev_config = {};
    dev_config.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    dev_config.device_address = LP5562_ADDR;
    dev_config.scl_speed_hz = 400 * 1000;
    ESP_ERROR_CHECK(i2c_master_bus_add_device(bus, &dev_config, &backlight));

    lp5562_write(LP5562_REG_ENABLE, 0x40); // chip enable
    vTaskDelay(pdMS_TO_TICKS(1));
    lp5562_write(LP5562_REG_CONFIG, 0x01);  // internal clock
    lp5562_write(LP5562_REG_LED_MAP, 0x00); // all LEDs driven by the PWM registers
}

void init_lcd()
{
    gpio_config_t rst_config = {};
    rst_config.pin_bit_mask = 1ULL << PIN_RST;
    rst_config.mode = GPIO_MODE_OUTPUT;
    ESP_ERROR_CHECK(gpio_config(&rst_config));

    spi_bus_config_t bus_config = {};
    bus_config.mosi_io_num = PIN_MOSI;
    bus_config.miso_io_num = GPIO_NUM_NC;
    bus_config.sclk_io_num = PIN_SCLK;
    bus_config.quadwp_io_num = GPIO_NUM_NC;
    bus_config.quadhd_io_num = GPIO_NUM_NC;
    bus_config.max_transfer_sz = FB_BYTES;
    ESP_ERROR_CHECK(spi_bus_initialize(LCD_HOST, &bus_config, SPI_DMA_CH_AUTO));

    esp_lcd_panel_io_spi_config_t io_config = {};
    io_config.cs_gpio_num = PIN_CS;
    io_config.dc_gpio_num = PIN_DC;
    io_config.spi_mode = 0;
    io_config.pclk_hz = LCD_CLOCK_HZ;
    io_config.trans_queue_depth = 4;
    io_config.lcd_cmd_bits = 8;
    io_config.lcd_param_bits = 8;
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_HOST, &io_config, &io));

    gpio_set_level(PIN_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(PIN_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(120));

    for (const auto &c : INIT_CMDS) {
        ESP_ERROR_CHECK(esp_lcd_panel_io_tx_param(io, c.cmd, c.data, c.len));
        if (c.delay_ms) {
            vTaskDelay(pdMS_TO_TICKS(c.delay_ms));
        }
    }

    const uint16_t x0 = OFFSET_X, x1 = OFFSET_X + WIDTH - 1;
    const uint16_t y0 = OFFSET_Y, y1 = OFFSET_Y + HEIGHT - 1;
    const uint8_t caset[] = {uint8_t(x0 >> 8), uint8_t(x0), uint8_t(x1 >> 8), uint8_t(x1)};
    const uint8_t raset[] = {uint8_t(y0 >> 8), uint8_t(y0), uint8_t(y1 >> 8), uint8_t(y1)};
    ESP_ERROR_CHECK(esp_lcd_panel_io_tx_param(io, 0x2A, caset, sizeof(caset)));
    ESP_ERROR_CHECK(esp_lcd_panel_io_tx_param(io, 0x2B, raset, sizeof(raset)));
}

} // namespace

void init()
{
    fb = static_cast<uint16_t *>(heap_caps_malloc(FB_BYTES, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));
    assert(fb);

    init_lcd();
    clear();
    flush();
    ESP_ERROR_CHECK(esp_lcd_panel_io_tx_param(io, 0x29, nullptr, 0)); // display on

    init_backlight();
    set_brightness(128);
}

void set_brightness(uint8_t brightness)
{
    lp5562_write(LP5562_REG_W_PWM, brightness);
}

void clear(uint16_t color)
{
    const uint16_t wire = to_wire(color);
    for (int i = 0; i < WIDTH * HEIGHT; i++) {
        fb[i] = wire;
    }
}

void draw_char(int x, int y, char c, uint16_t fg, uint16_t bg)
{
    if (c < font5x7::FIRST || c > font5x7::LAST) {
        c = '?';
    }
    const uint8_t *glyph = font5x7::GLYPHS[c - font5x7::FIRST];
    const uint16_t fg_wire = to_wire(fg), bg_wire = to_wire(bg);

    for (int dy = 0; dy < CELL_H; dy++) {
        for (int dx = 0; dx < CELL_W; dx++) {
            const int px = x + dx, py = y + dy;
            if (px < 0 || px >= WIDTH || py < 0 || py >= HEIGHT) {
                continue;
            }
            const bool on = dx < font5x7::WIDTH && dy < font5x7::HEIGHT && (glyph[dx] >> dy & 1);
            fb[py * WIDTH + px] = on ? fg_wire : bg_wire;
        }
    }
}

void draw_text(int x, int y, const char *text, uint16_t fg, uint16_t bg)
{
    const int left = x;
    for (; *text; text++) {
        if (*text == '\n' || x + CELL_W > WIDTH) {
            x = left;
            y += CELL_H;
            if (*text == '\n') {
                continue;
            }
        }
        draw_char(x, y, *text, fg, bg);
        x += CELL_W;
    }
}

void flush()
{
    ESP_ERROR_CHECK(esp_lcd_panel_io_tx_color(io, 0x2C, fb, FB_BYTES));
}

} // namespace display
