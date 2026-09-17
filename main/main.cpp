#include <cstdio>
#include <cstring>

#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "chat.hpp"
#include "display.hpp"

namespace
{

// The button under the screen, active low
constexpr gpio_num_t PIN_BUTTON = GPIO_NUM_41;

struct Message
{
    chat::Speaker speaker;
    const char *text;
};

// Canned conversation until there is a real LLM on the other end
constexpr Message SCRIPT[] = {
    {chat::Speaker::User, "Hello!"},
    {chat::Speaker::Assistant, "Hi there. What can I do for you today?"},
    {chat::Speaker::User, "What are you running on?"},
    {chat::Speaker::Assistant, "An M5Stack AtomS3R: an ESP32-S3 with 8MB of flash, 8MB of PSRAM and a 128x128 pixel screen."},
    {chat::Speaker::User, "Can you fit a whole conversation on that?"},
    {chat::Speaker::Assistant, "Not all at once. With a 5x7 font I get 21 columns and about 14 lines, so older messages scroll off the top."},
    {chat::Speaker::User, "Show me a list."},
    {chat::Speaker::Assistant, "Sure:\n- one\n- two\n- three"},
    {chat::Speaker::User, "And a word that is too long to wrap?"},
    {chat::Speaker::Assistant, "Pneumonoultramicroscopicsilicovolcanoconiosis."},
    {chat::Speaker::User, "Thanks, bye."},
    {chat::Speaker::Assistant, "Bye! Press the button to start over."},
};
constexpr int SCRIPT_LEN = sizeof(SCRIPT) / sizeof(SCRIPT[0]);

// Feeds text to the chat a few characters at a time, like tokens from a model
void stream(const char *text)
{
    constexpr int PIECE_LEN = 3;
    char piece[PIECE_LEN + 1];
    while (*text) {
        strlcpy(piece, text, sizeof(piece));
        chat::append(piece);
        text += strlen(piece);
        vTaskDelay(pdMS_TO_TICKS(60));
    }
}

void wait_for_press()
{
    // Debounce: the level has to hold for a few polls in a row
    auto wait_for_level = [](int level) {
        for (int stable = 0; stable < 3;) {
            stable = gpio_get_level(PIN_BUTTON) == level ? stable + 1 : 0;
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    };
    wait_for_level(1);
    wait_for_level(0);
}

} // namespace

extern "C" void app_main(void)
{
    gpio_config_t button_config = {};
    button_config.pin_bit_mask = 1ULL << PIN_BUTTON;
    button_config.mode = GPIO_MODE_INPUT;
    button_config.pull_up_en = GPIO_PULLUP_ENABLE;
    ESP_ERROR_CHECK(gpio_config(&button_config));

    display::init();

    for (int i = 0;; i = (i + 1) % SCRIPT_LEN) {
        if (i == 0) {
            chat::init();
        }
        if (SCRIPT[i].speaker == chat::Speaker::User) {
            chat::add(SCRIPT[i].speaker, SCRIPT[i].text);
        } else {
            chat::begin(SCRIPT[i].speaker);
            stream(SCRIPT[i].text);
        }
        wait_for_press();
    }
}
