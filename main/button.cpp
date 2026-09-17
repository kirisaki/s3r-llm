#include "button.hpp"

#include <atomic>

#include "driver/gpio.h"
#include "esp_attr.h"
#include "esp_err.h"
#include "esp_timer.h"

namespace button
{

namespace
{

// Active low
constexpr gpio_num_t PIN = GPIO_NUM_41;
constexpr int64_t DEBOUNCE_US = 200 * 1000;

std::atomic<bool> flag{false};
int64_t last_edge = 0;

void IRAM_ATTR on_falling_edge(void *)
{
    const int64_t now = esp_timer_get_time();
    if (now - last_edge > DEBOUNCE_US) {
        flag = true;
    }
    last_edge = now;
}

} // namespace

void init()
{
    gpio_config_t config = {};
    config.pin_bit_mask = 1ULL << PIN;
    config.mode = GPIO_MODE_INPUT;
    config.pull_up_en = GPIO_PULLUP_ENABLE;
    config.intr_type = GPIO_INTR_NEGEDGE;
    ESP_ERROR_CHECK(gpio_config(&config));
    ESP_ERROR_CHECK(gpio_install_isr_service(0));
    ESP_ERROR_CHECK(gpio_isr_handler_add(PIN, on_falling_edge, nullptr));
}

bool pressed()
{
    return flag.exchange(false);
}

} // namespace button
