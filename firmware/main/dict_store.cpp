#include "dict_store.hpp"

#include <algorithm>
#include <atomic>
#include <cstring>
#include <dirent.h>
#include <string>
#include "esp_log.h"
#include "esp_partition.h"
#include "app_config.hpp"
#include "file_replace.hpp"

static const char* TAG = "dict_store";
static std::atomic<bool> s_reload{false};
static std::atomic<bool> s_pause{false}, s_paused{false};
void dict_store::requestReload() { s_reload = true; }
bool dict_store::reloadPending() { return s_reload.exchange(false); }
void dict_store::requestTransferPause() { s_paused = false; s_pause = true; }
bool dict_store::transferPauseRequested() { return s_pause; }
void dict_store::acknowledgeTransferPause() { s_paused = true; }
bool dict_store::transferPaused() { return s_paused; }
void dict_store::endTransferPause() { s_pause = false; }

// [book:7-load-flash]
// [book:7-load-flash-start]
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

    // Flash is unchanged during a running firmware. Reuse one mapping for its
    // lifetime instead of leaking a mapping handle on every SD transfer/reload.
    static const void* mapped = nullptr;
    static uint32_t mappedSize = 0;
    esp_partition_mmap_handle_t handle = 0;
// [/book:7-load-flash-start]
    const void* ptr = mapped;
    const bool newMapping = !ptr;
    if (newMapping) {
        esp_err_t err = esp_partition_mmap(part, 0, total, ESP_PARTITION_MMAP_DATA, &ptr, &handle);
// [book:7-load-flash-end]
        if (err != ESP_OK) { ESP_LOGE(TAG, "mmap failed: %s", esp_err_to_name(err)); return false; }
    } else if (total != mappedSize) return false;

    auto d = std::make_unique<Dictionary>();
    if (!d->openMemory(static_cast<const uint8_t*>(ptr), total, "flash")) {
        if (newMapping) esp_partition_munmap(handle);
        return false;
    }
    if (newMapping) { mapped = ptr; mappedSize = total; }
    out.push_back(std::move(d));
    return true;
}
// [/book:7-load-flash-end]
// [/book:7-load-flash]

// [book:11-load-sd]
static size_t loadSd(std::vector<std::unique_ptr<Dictionary>>& out)
{
    file_replace::recoverDirectory(cfg::SD_DICT_DIR);
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
