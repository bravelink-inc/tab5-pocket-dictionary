#pragma once
#include "nvs.h"
constexpr int ESP_ERR_NVS_NO_FREE_PAGES = 5, ESP_ERR_NVS_NEW_VERSION_FOUND = 6;
int nvs_flash_init();
int nvs_flash_erase();
inline const char* esp_err_to_name(int) { return "test error"; }
