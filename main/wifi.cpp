#include "wifi.hpp"

#include <atomic>
#include <cstdio>
#include <cstring>
#include <initializer_list>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_mac.h"
#include "esp_wifi.h"
#include "mdns.h"
#include "mdns.h"

#include "settings.hpp"

namespace wifi
{

namespace
{

constexpr char TAG[] = "wifi";
constexpr size_t MAX_NAME = 31;

std::atomic<bool> connected{false};
bool have_credentials = false;
char ip_text[16];
char ssid_text[33];
std::atomic<int> last_reason{0};
esp_netif_t *netif = nullptr;
char device_name[MAX_NAME + 1];
bool name_is_set = false;

void on_event(void *, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        connected = false;
        last_reason = static_cast<wifi_event_sta_disconnected_t *>(data)->reason;
        if (have_credentials) {
            esp_wifi_connect();
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        const auto *event = static_cast<ip_event_got_ip_t *>(data);
        esp_ip4addr_ntoa(&event->ip_info.ip, ip_text, sizeof(ip_text));
        connected = true;
    }
}

bool valid_name(const char *name)
{
    const size_t len = strlen(name);
    if (len == 0 || len > MAX_NAME || name[0] == '-' || name[len - 1] == '-') {
        return false;
    }
    for (const char *c = name; *c; c++) {
        if (!(*c >= 'a' && *c <= 'z') && !(*c >= '0' && *c <= '9') && *c != '-') {
            return false;
        }
    }
    return true;
}

void connect(const char *ssid, const char *password)
{
    wifi_config_t config = {};
    strlcpy(reinterpret_cast<char *>(config.sta.ssid), ssid, sizeof(config.sta.ssid));
    strlcpy(reinterpret_cast<char *>(config.sta.password), password, sizeof(config.sta.password));
    strlcpy(ssid_text, ssid, sizeof(ssid_text));
    last_reason = 0;

    // With credentials in place the driver is either connected or trying to
    // be; dropping that makes on_event() connect again, with the new config.
    // Without, nothing is going on, and before esp_wifi_start() connecting
    // fails and is left to WIFI_EVENT_STA_START.
    const bool active = have_credentials;
    have_credentials = true;
    if (active) {
        esp_wifi_disconnect();
    }
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &config));
    if (!active) {
        esp_wifi_connect();
    }
}

} // namespace

void init()
{
    name_is_set = settings::get("name", device_name, sizeof(device_name)) && valid_name(device_name);
    if (!name_is_set) {
        uint8_t mac[6];
        ESP_ERROR_CHECK(esp_read_mac(mac, ESP_MAC_WIFI_STA));
        snprintf(device_name, sizeof(device_name), "s3r-llm-%02x%02x", mac[4], mac[5]);
    }

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    netif = esp_netif_create_default_wifi_sta();
    esp_netif_set_hostname(netif, device_name);

    wifi_init_config_t init_config = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init_config));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_event, nullptr, nullptr));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_event, nullptr, nullptr));
    // The credentials are kept with our own settings
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));

    char ssid[33], password[65];
    if (settings::get("wifi_ssid", ssid, sizeof(ssid)) && settings::get("wifi_password", password, sizeof(password))) {
        connect(ssid, password);
    } else {
        ESP_LOGI(TAG, "no credentials, set them with /wifi <ssid> <password>");
    }
    ESP_ERROR_CHECK(esp_wifi_start());
    // Power saving sleeps through multicast, which is what mDNS runs on, and
    // adds a tenth of a second to every request
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));

    // http://<name>.local/
    ESP_ERROR_CHECK(mdns_init());
    ESP_ERROR_CHECK(mdns_hostname_set(device_name));
    ESP_ERROR_CHECK(mdns_service_add(nullptr, "_http", "_tcp", 80, nullptr, 0));

    // The serial port is a conversation; keep the driver's chatter out of it
    for (const char *tag : {"wifi", "wifi_init", "phy_init", "pp", "net80211", "esp_netif_handlers"}) {
        esp_log_level_set(tag, ESP_LOG_WARN);
    }
}

void set_credentials(const char *ssid, const char *password)
{
    settings::set("wifi_ssid", ssid);
    settings::set("wifi_password", password);
    connect(ssid, password);
}

void forget()
{
    settings::erase("wifi_ssid");
    settings::erase("wifi_password");
    have_credentials = false;
    esp_wifi_disconnect();
}

bool configured()
{
    return have_credentials;
}

const char *ssid()
{
    return ssid_text;
}

const char *problem()
{
    switch (last_reason) {
    case 0:
        return "still trying";
    case WIFI_REASON_NO_AP_FOUND:
    case WIFI_REASON_NO_AP_FOUND_W_COMPATIBLE_SECURITY:
    case WIFI_REASON_NO_AP_FOUND_IN_AUTHMODE_THRESHOLD:
    case WIFI_REASON_NO_AP_FOUND_IN_RSSI_THRESHOLD:
        return "no such network on 2.4GHz";
    case WIFI_REASON_AUTH_FAIL:
    case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
    case WIFI_REASON_HANDSHAKE_TIMEOUT:
    case WIFI_REASON_MIC_FAILURE:
        return "wrong password?";
    default: {
        static char text[24];
        snprintf(text, sizeof(text), "reason %d", last_reason.load());
        return text;
    }
    }
}

const char *ip()
{
    return connected ? ip_text : nullptr;
}

const char *name()
{
    return device_name;
}

bool named()
{
    return name_is_set;
}

bool set_name(const char *name)
{
    if (!valid_name(name)) {
        return false;
    }
    strlcpy(device_name, name, sizeof(device_name));
    name_is_set = true;
    settings::set("name", device_name);
    // mDNS follows at once, DHCP with the next lease
    esp_netif_set_hostname(netif, device_name);
    ESP_ERROR_CHECK(mdns_hostname_set(device_name));
    return true;
}

} // namespace wifi
