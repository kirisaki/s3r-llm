#include <algorithm>
#include <cstdio>
#include <cstring>

#include "button.hpp"
#include "chat.hpp"
#include "display.hpp"
#include "imu.hpp"
#include "llm.hpp"
#include "serial.hpp"

namespace
{

// Shaking the device heats the sampler up: at rest the model is as sensible as
// it gets, a good shake turns the reply into word salad.
constexpr float TEMPERATURE_PER_G = 1.0f;
constexpr float MAX_TEMPERATURE = 3.0f;

// Toggled with "/stats"
bool show_stats = false;

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
    if (show_stats) {
        char line[128];
        snprintf(line, sizeof(line), "[prompt: %d tokens in %d ms, reply: %d tokens in %d ms (%.1f tok/s), temperature up to %.1f]\r\n",
                 stats.prompt_tokens, stats.prompt_ms, stats.reply_tokens, stats.reply_ms,
                 stats.reply_ms ? 1000.0f * stats.reply_tokens / stats.reply_ms : 0.0f, hottest);
        serial::write(line);
    }
    return !cancelled;
}

void new_conversation()
{
    llm::reset();
    chat::init();
    serial::write("[new conversation]\r\n");
}

// Lines starting with a slash are for the device, not for the model.
bool run_command(const char *line)
{
    if (strcmp(line, "/new") == 0) {
        new_conversation();
    } else if (strcmp(line, "/stats") == 0) {
        show_stats = !show_stats;
        serial::write(show_stats ? "[stats on]\r\n" : "[stats off]\r\n");
    } else if (line[0] == '/') {
        serial::write("[commands: /new /stats]\r\n");
    } else {
        return false;
    }
    return true;
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
            serial::write("\r\n");
            new_conversation();
        } else if (serial::poll_line(prompt, sizeof(prompt), 50)) {
            input = run_command(prompt) ? nullptr : prompt;
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
