#pragma once
#include <cstddef>
#include <cstdint>
using esp_err_t = int;
using nvs_handle_t = int;
constexpr int ESP_OK = 0, ESP_ERR_NVS_NOT_FOUND = 1, ESP_ERR_INVALID_SIZE = 2;
constexpr int NVS_READONLY = 0, NVS_READWRITE = 1;
int nvs_open(const char*, int, nvs_handle_t*);
int nvs_get_blob(nvs_handle_t, const char*, void*, size_t*);
int nvs_set_blob(nvs_handle_t, const char*, const void*, size_t);
int nvs_erase_key(nvs_handle_t, const char*);
int nvs_commit(nvs_handle_t);
void nvs_close(nvs_handle_t);
int nvs_get_i8(nvs_handle_t, const char*, int8_t*);
int nvs_set_i8(nvs_handle_t, const char*, int8_t);
