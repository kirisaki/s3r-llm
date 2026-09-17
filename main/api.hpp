#pragma once

#include <cstddef>

// HTTP API, plain text both ways:
//
//   POST /chat   the body is the message; the reply is streamed back
//   POST /new    starts a new conversation
//   POST /talk   the body is "<device> [opening line]"; has this device talk
//                to the other one on its own
//   POST /stop   cuts short whatever is being said, and ends a talk
//
// The requests are only queued here. The main loop picks them up, because it
// owns the model and the screen.
namespace api
{

// Needs the network stack, so after wifi::init().
void start();

// A message waiting for a reply, if there is one. Every piece of the reply
// then goes to send(), and finish() ends the response.
bool poll_chat(char *buf, size_t size);
void send(const char *piece);
void finish();

// Whether POST /new or POST /stop have been called since the last call.
bool poll_new();
bool poll_stop();
// The body of a POST /talk, if there has been one.
bool poll_talk(char *buf, size_t size);

} // namespace api
