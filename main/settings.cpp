#include "settings.hpp"

#include "esp_err.h"
#include "nvs.h"
#include "nvs_flash.h"

namespace settings
{

namespace
{

constexpr char NVS_NAMESPACE[] = "s3r-llm";

nvs_handle_t open(nvs_open_mode_t mode)
{
    nvs_handle_t nvs;
    ESP_ERROR_CHECK(nvs_open(NVS_NAMESPACE, mode, &nvs));
    return nvs;
}

} // namespace

void init()
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
}

bool get(const char *key, char *buf, size_t size)
{
    // Opened for writing so that the namespace exists from the first read on
    const nvs_handle_t nvs = open(NVS_READWRITE);
    const bool found = nvs_get_str(nvs, key, buf, &size) == ESP_OK;
    nvs_close(nvs);
    return found;
}

void set(const char *key, const char *value)
{
    const nvs_handle_t nvs = open(NVS_READWRITE);
    ESP_ERROR_CHECK(nvs_set_str(nvs, key, value));
    ESP_ERROR_CHECK(nvs_commit(nvs));
    nvs_close(nvs);
}

void erase(const char *key)
{
    const nvs_handle_t nvs = open(NVS_READWRITE);
    nvs_erase_key(nvs, key);
    ESP_ERROR_CHECK(nvs_commit(nvs));
    nvs_close(nvs);
}

} // namespace settings
