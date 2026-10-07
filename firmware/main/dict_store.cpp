#include "dict_store.hpp"

#include <algorithm>
#include <atomic>
#include <cstring>
#include <dirent.h>
#include <string>
#include "esp_log.h"
#include "esp_partition.h"
#include "app_config.hpp"

static const char* TAG = "dict_store";
static std::atomic<bool> s_reload{false};
void dict_store::requestReload() { s_reload = true; }
bool dict_store::reloadPending() { return s_reload.exchange(false); }

// [book:7-load-flash]
static bool loadFlash(std::vector<std::unique_ptr<Dictionary>>& out)
{
    const esp_partition_t* part = esp_partition_find_first(ESP_PARTITION_TYPE_ANY, ESP_PARTITION_SUBTYPE_ANY, cfg::DICT_PARTITION_LABEL);
    if (!part) { ESP_LOGW(TAG, "no '%s' partition", cfg::DICT_PARTITION_LABEL); return false; }

    uint8_t hdr[64];
    if (esp_partition_read(part, 0, hdr, sizeof(hdr)) != ESP_OK || memcmp(hdr, "PDC1", 4) != 0) {
        ESP_LOGW(TAG, "flash partition holds no dictionary image (run `idf.py flash`)");
        return false;
    }
    uint32_t total; memcpy(&total, hdr + 44, 4);
    if (total < 64 || total > part->size) { ESP_LOGW(TAG, "flash image size %lu invalid", (unsigned long)total); return false; }

    const void* ptr = nullptr;
    esp_partition_mmap_handle_t handle;
    esp_err_t err = esp_partition_mmap(part, 0, total, ESP_PARTITION_MMAP_DATA, &ptr, &handle);
    if (err != ESP_OK) { ESP_LOGE(TAG, "mmap failed: %s", esp_err_to_name(err)); return false; }

    auto d = std::make_unique<Dictionary>();
    if (!d->openMemory(static_cast<const uint8_t*>(ptr), total, "flash")) { esp_partition_munmap(handle); return false; }
    out.push_back(std::move(d));
    return true;
}
// [/book:7-load-flash]

// [book:11-load-sd]
static size_t loadSd(std::vector<std::unique_ptr<Dictionary>>& out)
{
    DIR* dir = opendir(cfg::SD_DICT_DIR);
    if (!dir) { ESP_LOGI(TAG, "%s not found", cfg::SD_DICT_DIR); return 0; }

    std::vector<std::string> names;
    while (dirent* e = readdir(dir)) {
        std::string n = e->d_name;
        if (n.size() < 5) continue;
        std::string ext = n.substr(n.size() - 4);
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        if (ext == ".pdc") names.push_back(n);
    }
    closedir(dir);
    std::sort(names.begin(), names.end());

    size_t loaded = 0;
    for (const auto& n : names) {
        std::string path = std::string(cfg::SD_DICT_DIR) + "/" + n;
        auto d = std::make_unique<Dictionary>();
        if (d->openFile(path.c_str())) { out.push_back(std::move(d)); ++loaded; }
    }
    return loaded;
}
// [/book:11-load-sd]

size_t dict_store::loadAll(std::vector<std::unique_ptr<Dictionary>>& out)
{
    out.clear();
    loadFlash(out);
    loadSd(out);
    ESP_LOGI(TAG, "%u dictionaries loaded", (unsigned)out.size());
    return out.size();
}
