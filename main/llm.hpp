#pragma once

#include <functional>

// The on-device model: TinyTalk 2, read from the "model" and "tokenizer"
// partitions. It remembers the conversation for as long as it fits into the
// model's 256 positions and starts over when it no longer does.
namespace llm
{

// Receives the reply piece by piece as it is generated. Pieces are ASCII.
using Sink = std::function<void(const char *piece)>;

struct Stats
{
    int prompt_tokens;
    int reply_tokens;
    int prompt_ms;
    int reply_ms;
};

void init();
// Blocks until the reply to prompt is complete.
Stats generate(const char *prompt, const Sink &sink);

} // namespace llm
