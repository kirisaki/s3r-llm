#include <algorithm>
#include <cstdio>
#include <cstring>

#include "api.hpp"
#include "button.hpp"
#include "chat.hpp"
#include "display.hpp"
#include "imu.hpp"
#include "llm.hpp"
#include "serial.hpp"
#include "settings.hpp"
#include "wifi.hpp"

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

// Returns false if the reply was cut short to start a new conversation.
bool respond(const char *prompt, bool to_api)
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
        if (to_api) {
            api::send(piece);
        }
        llm::set_temperature(t);
        cancelled = button::pressed() || api::poll_new();
        return !cancelled;
    });
    serial::write("\r\n");
    if (to_api) {
        api::finish();
    }
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

// "/wifi", "/wifi off" or "/wifi <ssid> <password>"; an SSID with spaces in it
// goes into double quotes.
void wifi_command(char *args)
{
    while (*args == ' ') {
        args++;
    }
    if (*args == '\0') {
        char line[96];
        if (wifi::ip()) {
            snprintf(line, sizeof(line), "[wifi: http://%s.local/ or http://%s/]\r\n", wifi::name(), wifi::ip());
        } else if (wifi::configured()) {
            snprintf(line, sizeof(line), "[wifi: not connected to \"%s\", %s]\r\n", wifi::ssid(), wifi::problem());
        } else {
            snprintf(line, sizeof(line), "[wifi: not set up]\r\n");
        }
        serial::write(line);
        return;
    }
    if (strcmp(args, "off") == 0) {
        wifi::forget();
        serial::write("[wifi: credentials forgotten]\r\n");
        return;
    }

    char *ssid = args;
    char *end = strchr(args, ' ');
    if (*args == '"') {
        ssid = args + 1;
        end = strchr(ssid, '"');
    }
    if (!end) {
        serial::write("[usage: /wifi <ssid> <password>]\r\n");
        return;
    }
    *end = '\0';
    char *password = end + 1;
    while (*password == ' ') {
        password++;
    }
    wifi::set_credentials(ssid, password);
    serial::write("[wifi: connecting]\r\n");
}

// "/name" or "/name <name>"
void name_command(const char *args)
{
    while (*args == ' ') {
        args++;
    }
    if (*args != '\0' && !wifi::set_name(args)) {
        serial::write("[a name is up to 31 lowercase letters, digits and hyphens]\r\n");
        return;
    }
    char line[64];
    snprintf(line, sizeof(line), "[name: %s]\r\n", wifi::name());
    serial::write(line);
}

// Lines starting with a slash are for the device, not for the model.
bool run_command(char *line)
{
    if (strcmp(line, "/new") == 0) {
        new_conversation();
    } else if (strcmp(line, "/stats") == 0) {
        show_stats = !show_stats;
        serial::write(show_stats ? "[stats on]\r\n" : "[stats off]\r\n");
    } else if (strncmp(line, "/wifi", 5) == 0 && (line[5] == ' ' || line[5] == '\0')) {
        wifi_command(line + 5);
    } else if (strncmp(line, "/name", 5) == 0 && (line[5] == ' ' || line[5] == '\0')) {
        name_command(line + 5);
    } else if (line[0] == '/') {
        serial::write("[commands: /new /stats /wifi /name]\r\n");
    } else {
        return false;
    }
    return true;
}

// Says where the API is, once per address
void announce_wifi()
{
    static char announced[16];
    const char *ip = wifi::ip();
    if (!ip || strcmp(ip, announced) == 0) {
        return;
    }
    strlcpy(announced, ip, sizeof(announced));
    char line[96];
    snprintf(line, sizeof(line), "%s.local\n%s", wifi::name(), ip);
    chat::add(chat::Speaker::System, line);
    snprintf(line, sizeof(line), "\r\n[wifi: http://%s.local/ or http://%s/]\r\n> ", wifi::name(), ip);
    serial::write(line);
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
    settings::init();
    wifi::init();
    api::start();

    static char prompt[256];
    serial::write("> ");
    for (;;) {
        if (!serial::typing()) {
            announce_wifi();
        }
        // Motion speaks for the user, unless they are in the middle of a line
        const char *motion = serial::typing() ? nullptr : describe(imu::poll_event());
        const char *input = nullptr;
        bool to_api = false;
        if (button::pressed() || api::poll_new()) {
            serial::write("\r\n");
            new_conversation();
        } else if (serial::poll_line(prompt, sizeof(prompt), 50)) {
            input = run_command(prompt) ? nullptr : prompt;
        } else if (!serial::typing() && api::poll_chat(prompt, sizeof(prompt))) {
            input = prompt;
            to_api = true;
        } else if (motion) {
            input = motion;
        } else {
            continue;
        }

        if (input && input[0] != '\0') {
            if (input != prompt || to_api) {
                // Not typed here, so not echoed yet
                serial::write(input);
                serial::write("\r\n");
            }
            if (!respond(input, to_api)) {
                new_conversation();
            }
        }
        imu::poll_event(); // whatever happened during the reply is stale
        serial::write("> ");
    }
}
