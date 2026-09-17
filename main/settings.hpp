#pragma once

#include <cstddef>

// Per-device settings in NVS: what must not end up in the repository or the
// firmware image, and what tells one device from another.
namespace settings
{

void init();
// False if the key has never been set; buf is left alone then.
bool get(const char *key, char *buf, size_t size);
void set(const char *key, const char *value);
void erase(const char *key);

} // namespace settings
