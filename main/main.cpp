#include <algorithm>

#include "esp_log.h"

#include "button.hpp"
#include "chat.hpp"
#include "display.hpp"
#include "imu.hpp"
#include "llm.hpp"
#include "serial.hpp"

namespace
{

constexpr char TAG[] = "main";

// Shaking the device heats the sampler up: at rest the model is as sensible as
// it gets, a good shake turns the reply into word salad.
constexpr float TEMPERATURE_PER_G = 1.0f;
constexpr float MAX_TEMPERATURE = 3.0f;

float temperature()
{
    return std::min(llm::DEFAULT_TEMPERATURE + TEMPERATURE_PER_G * imu::shake_level(), MAX_TEMPERATURE);
}

// White at rest, towards orange-red as the temperature rises
uint16_t temperature_color(float t)
{
    const float heat = (t - llm::DEFAULT_TEMPERATURE) / (MAX_TEMPERATURE - llm::DEFAULT_TEMPERATURE);
    return display::rgb565(255, 255 - 175 * heat, 255 - 255 * heat);
}

// What the model gets told when the device is moved
const char *describe(imu::Event event)
{
    switch (event) {
    case imu::Event::Shake:
        return "I am shaking you!";
    case imu::Event::FaceDown:
        return "I turned you upside down!";
    case imu::Event::FaceUp:
        return "I turned you back up.";
    default:
        return nullptr;
    }
}

// Returns false if the button cut the reply short.
bool respond(const char *prompt)
{
    chat::add(chat::Speaker::User, prompt);
    chat::begin(chat::Speaker::Assistant);

    llm::set_temperature(temperature());
    float hottest = 0.0f;
    bool cancelled = false;
    const llm::Stats stats = llm::generate(prompt, [&](const char *piece) {
        // The piece was sampled at the temperature set before it
        const float t = temperature();
        hottest = std::max(hottest, t);
        chat::append(piece, temperature_color(t));
        serial::write(piece);
        llm::set_temperature(t);
        cancelled = button::pressed();
        return !cancelled;
    });
    serial::write("\r\n");
    ESP_LOGI(TAG, "prompt: %d tokens in %d ms, reply: %d tokens in %d ms (%.1f tok/s), temperature up to %.1f",
             stats.prompt_tokens, stats.prompt_ms, stats.reply_tokens, stats.reply_ms,
             stats.reply_ms ? 1000.0f * stats.reply_tokens / stats.reply_ms : 0.0f, hottest);
    return !cancelled;
}

void new_conversation()
{
    llm::reset();
    chat::init();
    serial::write("\r\n[new conversation]\r\n");
}

} // namespace

extern "C" void app_main(void)
{
    serial::init();
    display::init();
    chat::init();
    llm::init();
    imu::init();
    button::init();

    static char prompt[256];
    serial::write("> ");
    for (;;) {
        // Motion speaks for the user, unless they are in the middle of a line
        const char *motion = serial::typing() ? nullptr : describe(imu::poll_event());
        const char *input = nullptr;
        if (button::pressed()) {
            new_conversation();
        } else if (serial::poll_line(prompt, sizeof(prompt), 50)) {
            input = prompt;
        } else if (motion) {
            serial::write(motion);
            serial::write("\r\n");
            input = motion;
        } else {
            continue;
        }

        if (input && input[0] != '\0' && !respond(input)) {
            new_conversation();
        }
        imu::poll_event(); // whatever happened during the reply is stale
        serial::write("> ");
    }
}
