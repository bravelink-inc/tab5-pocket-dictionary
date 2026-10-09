#include "settings.hpp"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"

static const char* TAG = "settings";
static const char* NS = "pdict";

void settings_init()
{
    esp_err_t err = nvs_flash_init();
    // Do not erase the user's wordbook/rotation automatically on a failed init.
    // Recovery that resets NVS is an explicit, documented operation.
    if (err != ESP_OK) ESP_LOGW(TAG, "nvs init failed: %s", esp_err_to_name(err));
}

// [book:14-rotation]
int settings_get_rotation(int fallback)
{
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READONLY, &h) != ESP_OK) return fallback;
    int8_t v = static_cast<int8_t>(fallback);
    nvs_get_i8(h, "rotation", &v);
    nvs_close(h);
    return (v >= 0 && v <= 3) ? v : fallback;
}
// [/book:14-rotation]

void settings_set_rotation(int rotation)
{
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_i8(h, "rotation", static_cast<int8_t>(rotation));
    nvs_commit(h);
    nvs_close(h);
}
