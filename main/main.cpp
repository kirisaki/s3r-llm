#include "esp_log.h"

#include "chat.hpp"
#include "display.hpp"
#include "llm.hpp"
#include "serial.hpp"

static const char TAG[] = "main";

extern "C" void app_main(void)
{
    serial::init();
    display::init();
    chat::init();
    llm::init();

    static char prompt[256];
    for (;;) {
        serial::write("> ");
        if (serial::read_line(prompt, sizeof(prompt)) == 0) {
            continue;
        }
        chat::add(chat::Speaker::User, prompt);

        chat::begin(chat::Speaker::Assistant);
        const llm::Stats stats = llm::generate(prompt, [](const char *piece) {
            chat::append(piece);
            serial::write(piece);
        });
        serial::write("\r\n");
        ESP_LOGI(TAG, "prompt: %d tokens in %d ms, reply: %d tokens in %d ms (%.1f tok/s)", stats.prompt_tokens,
                 stats.prompt_ms, stats.reply_tokens, stats.reply_ms,
                 stats.reply_ms ? 1000.0f * stats.reply_tokens / stats.reply_ms : 0.0f);
    }
}
