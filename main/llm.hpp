#pragma once

#include <functional>

namespace llm
{

// Receives the reply piece by piece as it is generated.
using Sink = std::function<void(const char *piece)>;

// Blocks until the reply to prompt is complete.
void generate(const char *prompt, const Sink &sink);

} // namespace llm
