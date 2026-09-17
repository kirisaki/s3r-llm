#include <algorithm>
#include <cstdio>
#include <cstring>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "api.hpp"
#include "button.hpp"
#include "chat.hpp"
#include "display.hpp"
#include "imu.hpp"
#include "llm.hpp"
#include "peer.hpp"
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

// Added while a talk between two devices is going in circles
float temperature_boost = 0.0f;

float temperature()
{
    return std::min(llm::DEFAULT_TEMPERATURE + TEMPERATURE_PER_G * imu::shake_level() + temperature_boost,
                    MAX_TEMPERATURE);
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

// What can cut a reply short
enum class Interrupt
{
    None,
    // Just stop talking
    Stop,
    // And forget the conversation
    New,
};

Interrupt poll_interrupt()
{
    if (button::pressed() || api::poll_new()) {
        return Interrupt::New;
    }
    return api::poll_stop() ? Interrupt::Stop : Interrupt::None;
}

// Generates the reply to prompt onto the screen, the serial port and, if that is
// where the prompt came from, the API.
Interrupt answer(const char *prompt, bool to_api, char *reply, size_t reply_size)
{
    chat::begin(chat::Speaker::Assistant);
    if (reply) {
        reply[0] = '\0';
    }

    llm::set_temperature(temperature());
    float hottest = 0.0f;
    Interrupt interrupt = Interrupt::None;
    const llm::Stats stats = llm::generate(prompt, [&](const char *piece) {
        // The piece was sampled at the temperature set before it
        const float t = temperature();
        hottest = std::max(hottest, t);
        chat::append(piece, temperature_color(t));
        serial::write(piece);
        if (to_api) {
            api::send(piece);
        }
        if (reply) {
            strlcat(reply, piece, reply_size);
        }
        llm::set_temperature(t);
        interrupt = poll_interrupt();
        return interrupt == Interrupt::None;
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
    return interrupt;
}

// Shows prompt as said by the user and answers it. The reply is also copied to
// reply, if there is one.
Interrupt respond(const char *prompt, bool to_api, char *reply = nullptr, size_t reply_size = 0)
{
    chat::add(chat::Speaker::User, prompt);
    return answer(prompt, to_api, reply, reply_size);
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

// A talk with another device. This one keeps it going on its own: it sends
// what it says to the API of the other one, and answers what comes back. The
// other device needs to know nothing about it.
constexpr int TALK_MAX_TURNS = 20;
constexpr TickType_t TALK_PAUSE = pdMS_TO_TICKS(1500);
constexpr float TALK_LOOP_BOOST = 0.7f;

struct Talk
{
    bool active = false;
    char peer[32];
    char ip[16];
    // What this device says next, and what it said and heard the turn before
    char line[256];
    char said_before[256];
    char heard_before[256];
    int turns = 0;
    TickType_t next_turn = 0;
};
Talk talk;

void stop_talk(const char *why)
{
    if (!talk.active) {
        return;
    }
    talk.active = false;
    temperature_boost = 0.0f;
    char line[96];
    snprintf(line, sizeof(line), "[talk with %s: %s]\r\n", talk.peer, why);
    serial::write(line);
}

// "<device> [opening line]"
void start_talk(char *args)
{
    while (*args == ' ') {
        args++;
    }
    char *opening = strchr(args, ' ');
    if (opening) {
        *opening++ = '\0';
        while (*opening == ' ') {
            opening++;
        }
    }
    if (*args == '\0') {
        serial::write("[usage: /talk <device> [opening line]]\r\n");
        return;
    }
    stop_talk("over");

    Talk next;
    strlcpy(next.peer, args, sizeof(next.peer));
    if (!wifi::ip()) {
        serial::write("[talk: no wifi]\r\n");
        return;
    }
    if (strcmp(next.peer, wifi::name()) == 0 || !peer::resolve(next.peer, next.ip, sizeof(next.ip))) {
        serial::write("[talk: no such device]\r\n");
        return;
    }
    if (strcmp(next.ip, wifi::ip()) == 0) {
        serial::write("[talk: that is me]\r\n");
        return;
    }
    strlcpy(next.line, opening && *opening ? opening : "Hello!", sizeof(next.line));
    next.said_before[0] = next.heard_before[0] = '\0';
    next.active = true;
    next.next_turn = xTaskGetTickCount();
    talk = next;

    llm::reset();
    chat::init();
    char line[96];
    snprintf(line, sizeof(line), "[talk with %s at %s]\r\n", talk.peer, talk.ip);
    serial::write(line);
    // The opening line is this device speaking, even if the model did not come up with it
    chat::add(chat::Speaker::Assistant, talk.line);
    serial::write(talk.line);
    serial::write("\r\n");
}

// One turn: say the line, hear the other device out, and think of a reply.
void talk_turn()
{
    char heard[256] = "";
    Interrupt interrupt = Interrupt::None;
    serial::write(talk.peer);
    serial::write(": ");
    chat::begin(chat::Speaker::User);
    const peer::Result result = peer::chat(talk.ip, talk.line, [&](const char *piece) {
        // Replies start with a space and end with a newline, neither of which is needed here
        if (heard[0] == '\0') {
            while (*piece == ' ') {
                piece++;
            }
        }
        if (*piece != '\0' && strcmp(piece, "\n") != 0) {
            strlcat(heard, piece, sizeof(heard));
            chat::append(piece);
            serial::write(piece);
        }
        interrupt = poll_interrupt();
        return interrupt == Interrupt::None;
    });
    serial::write("\r\n");

    if (result == peer::Result::Ok && heard[0] != '\0') {
        // Going in circles? Think a little less straight until that is over.
        const bool stuck = strcmp(heard, talk.heard_before) == 0 || strcmp(talk.line, talk.said_before) == 0;
        temperature_boost = stuck ? TALK_LOOP_BOOST : 0.0f;
        strlcpy(talk.heard_before, heard, sizeof(talk.heard_before));
        strlcpy(talk.said_before, talk.line, sizeof(talk.said_before));

        char reply[256];
        interrupt = answer(heard, false, reply, sizeof(reply));
        const char *line = reply;
        while (*line == ' ') {
            line++;
        }
        strlcpy(talk.line, line, sizeof(talk.line));
    }

    talk.turns++;
    talk.next_turn = xTaskGetTickCount() + TALK_PAUSE;
    if (interrupt == Interrupt::New) {
        stop_talk("stopped");
        new_conversation();
    } else if (interrupt == Interrupt::Stop) {
        stop_talk("stopped");
    } else if (result == peer::Result::Busy) {
        stop_talk("the other one is busy");
    } else if (result != peer::Result::Ok) {
        stop_talk("lost the other one");
    } else if (heard[0] == '\0' || talk.line[0] == '\0') {
        stop_talk("nothing more to say");
    } else if (talk.turns >= TALK_MAX_TURNS) {
        stop_talk("that will do");
    }
}

// "/name" or "/name <name>"
void name_command(const char *args)
{
    while (*args == ' ') {
        args++;
    }
    if (*args != '\0') {
        if (!wifi::set_name(args)) {
            serial::write("[a name is up to 31 lowercase letters, digits and hyphens]\r\n");
            return;
        }
        // The model goes by the new name from the next conversation on
        llm::set_name(wifi::name());
        chat::init();
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
    } else if (strncmp(line, "/talk", 5) == 0 && (line[5] == ' ' || line[5] == '\0')) {
        start_talk(line + 5);
    } else if (strcmp(line, "/stop") == 0) {
        stop_talk("stopped");
    } else if (line[0] == '/') {
        serial::write("[commands: /new /stats /wifi /name /talk /stop]\r\n");
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
    llm::set_name(wifi::named() ? wifi::name() : "");
    api::start();

    static char prompt[256];
    serial::write("> ");
    for (;;) {
        if (!serial::typing()) {
            announce_wifi();
        }
        // Motion speaks for the user, unless they are in the middle of a line
        // or the device is busy talking to another one
        const imu::Event event = imu::poll_event();
        const char *motion = serial::typing() || talk.active ? nullptr : describe(event);
        const char *input = nullptr;
        bool to_api = false;
        switch (poll_interrupt()) {
        case Interrupt::New:
            serial::write("\r\n");
            stop_talk("stopped");
            new_conversation();
            serial::write("> ");
            continue;
        case Interrupt::Stop:
            serial::write("\r\n");
            stop_talk("stopped");
            serial::write("> ");
            continue;
        case Interrupt::None:
            break;
        }

        if (serial::poll_line(prompt, sizeof(prompt), 50)) {
            if (!run_command(prompt)) {
                input = prompt;
            }
        } else if (!serial::typing() && api::poll_talk(prompt, sizeof(prompt))) {
            serial::write("\r\n");
            start_talk(prompt);
        } else if (!serial::typing() && api::poll_chat(prompt, sizeof(prompt))) {
            input = prompt;
            to_api = true;
        } else if (motion) {
            input = motion;
        } else if (talk.active && !serial::typing() && xTaskGetTickCount() >= talk.next_turn) {
            serial::write("\r");
            talk_turn();
        } else {
            continue;
        }

        if (input && input[0] != '\0') {
            // Somebody else wants a word
            stop_talk("interrupted");
            if (input != prompt || to_api) {
                // Not typed here, so not echoed yet
                serial::write(input);
                serial::write("\r\n");
            }
            // A /new or /stop from just before this input is not meant for its reply
            if (poll_interrupt() == Interrupt::New) {
                new_conversation();
            }
            if (respond(input, to_api) == Interrupt::New) {
                new_conversation();
            }
        }
        imu::poll_event(); // whatever happened during the reply is stale
        if (!talk.active) {
            serial::write("> ");
        }
    }
}
