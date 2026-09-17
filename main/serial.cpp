#include "serial.hpp"

#include <cstring>

#include "driver/usb_serial_jtag.h"
#include "driver/usb_serial_jtag_vfs.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"

namespace serial
{

namespace
{

constexpr TickType_t WRITE_TIMEOUT = pdMS_TO_TICKS(20);

// Last byte received, kept across lines to tell the LF of a CRLF from an empty line
char prev = '\0';
// Length of the line being entered
size_t line_len = 0;

void write_bytes(const char *data, size_t len)
{
    if (usb_serial_jtag_is_connected()) {
        usb_serial_jtag_write_bytes(data, len, WRITE_TIMEOUT);
    }
}

} // namespace

void init()
{
    usb_serial_jtag_driver_config_t config = USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(usb_serial_jtag_driver_install(&config));
    // Keep printf and the log going through the driver as well
    usb_serial_jtag_vfs_use_driver();
}

bool poll_line(char *buf, size_t size, int timeout_ms)
{
    const TickType_t start = xTaskGetTickCount();
    for (;;) {
        const TickType_t elapsed = xTaskGetTickCount() - start;
        if (elapsed >= pdMS_TO_TICKS(timeout_ms)) {
            return false;
        }
        char c;
        if (usb_serial_jtag_read_bytes(&c, 1, pdMS_TO_TICKS(timeout_ms) - elapsed) != 1) {
            return false;
        }
        const bool lf_of_crlf = c == '\n' && prev == '\r';
        prev = c;

        if (lf_of_crlf) {
            continue;
        }
        if (c == '\r' || c == '\n') {
            write_bytes("\r\n", 2);
            buf[line_len] = '\0';
            line_len = 0;
            return true;
        }
        if (c == '\b' || c == 0x7F) {
            if (line_len > 0) {
                line_len--;
                write_bytes("\b \b", 3);
            }
            continue;
        }
        if (c < 0x20 || c > 0x7E || line_len + 1 >= size) {
            continue;
        }
        buf[line_len++] = c;
        write_bytes(&c, 1);
    }
}

bool typing()
{
    return line_len > 0;
}

void write(const char *text)
{
    write_bytes(text, strlen(text));
}

} // namespace serial
