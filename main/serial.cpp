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

size_t read_line(char *buf, size_t size)
{
    size_t len = 0;
    for (;;) {
        char c;
        if (usb_serial_jtag_read_bytes(&c, 1, portMAX_DELAY) != 1) {
            continue;
        }
        const bool lf_of_crlf = c == '\n' && prev == '\r';
        prev = c;

        if (lf_of_crlf) {
            continue;
        }
        if (c == '\r' || c == '\n') {
            write_bytes("\r\n", 2);
            buf[len] = '\0';
            return len;
        }
        if (c == '\b' || c == 0x7F) {
            if (len > 0) {
                len--;
                write_bytes("\b \b", 3);
            }
            continue;
        }
        if (c < 0x20 || c > 0x7E || len + 1 >= size) {
            continue;
        }
        buf[len++] = c;
        write_bytes(&c, 1);
    }
}

void write(const char *text)
{
    write_bytes(text, strlen(text));
}

} // namespace serial
