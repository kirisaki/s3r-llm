#include "i2c_bus.hpp"

#include "esp_err.h"

namespace i2c_bus
{

namespace
{

constexpr gpio_num_t PIN_SDA = GPIO_NUM_45;
constexpr gpio_num_t PIN_SCL = GPIO_NUM_0;

i2c_master_bus_handle_t bus = nullptr;

} // namespace

i2c_master_bus_handle_t get()
{
    if (!bus) {
        i2c_master_bus_config_t config = {};
        config.i2c_port = -1;
        config.sda_io_num = PIN_SDA;
        config.scl_io_num = PIN_SCL;
        config.clk_source = I2C_CLK_SRC_DEFAULT;
        config.glitch_ignore_cnt = 7;
        config.flags.enable_internal_pullup = true;
        ESP_ERROR_CHECK(i2c_new_master_bus(&config, &bus));
    }
    return bus;
}

} // namespace i2c_bus
