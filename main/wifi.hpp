#pragma once

// WiFi station. The credentials and the name of the device live in NVS, not in
// the firmware.
namespace wifi
{

// Needs settings::init(). Connects in the background if there are credentials.
void init();
// Stores the credentials and connects with them.
void set_credentials(const char *ssid, const char *password);
// Forgets the credentials and disconnects.
void forget();

bool configured();
// The network the credentials are for.
const char *ssid();
// Why the last attempt to connect failed, in a few words.
const char *problem();
// Dotted address while connected, otherwise nullptr.
const char *ip();

// What the device is called on the network: "<name>.local" over mDNS, and the
// DHCP hostname. Tells several devices apart; "s3r-llm-" and the end of the
// MAC address until it is set.
const char *name();
// Whether the name has been set, rather than made up from the MAC address.
bool named();
// Lowercase letters, digits and hyphens, at most 31 of them. Returns false and
// changes nothing otherwise.
bool set_name(const char *name);

} // namespace wifi
