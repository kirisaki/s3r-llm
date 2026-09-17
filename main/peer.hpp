#pragma once

#include <cstddef>
#include <functional>

// The client side of the HTTP API: talking to another device like this one.
namespace peer
{

enum class Result
{
    Ok,
    // The other device is answering someone else
    Busy,
    Failed,
    // on_piece said so
    Stopped,
};

// Turns a device name ("bob", looked up as bob.local over mDNS) or a dotted
// address into a dotted address.
bool resolve(const char *name, char *ip, size_t size);

// Sends message to POST /chat of the device at ip and hands over the reply as
// it comes in. on_piece is called every now and then even without anything new
// (piece is "" then), so that it can stop a reply that is slow in coming.
Result chat(const char *ip, const char *message, const std::function<bool(const char *piece)> &on_piece);

} // namespace peer
