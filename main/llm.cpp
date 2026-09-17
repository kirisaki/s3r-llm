#include "llm.hpp"

#include <cstring>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace llm
{

namespace
{

// Hands text to the sink a few characters at a time, like tokens from a model
void stream(const char *text, const Sink &sink)
{
    constexpr int PIECE_LEN = 3;
    char piece[PIECE_LEN + 1];
    while (*text) {
        strlcpy(piece, text, sizeof(piece));
        sink(piece);
        text += strlen(piece);
        vTaskDelay(pdMS_TO_TICKS(60));
    }
}

} // namespace

// Placeholder until there is a real model
void generate(const char *prompt, const Sink &sink)
{
    stream("You said \"", sink);
    stream(prompt, sink);
    stream("\". I am only a placeholder until the real model moves in.", sink);
}

} // namespace llm
