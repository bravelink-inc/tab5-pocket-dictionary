#include "wordbook.hpp"

#include <algorithm>
#include <cstdio>
#include "esp_log.h"
#include "nvs.h"
#include "sdcard.hpp"
#include "app_config.hpp"

static const char* TAG = "wordbook";
static std::vector<std::string> s_words;
static const char* SD_PATH = "/sdcard/wordbook.txt";
static const char* NVS_NS = "pdict";
static const char* NVS_KEY = "wordbook";
static constexpr size_t NVS_MAX = 3900;   // NVS blob limit (~4000 bytes)

// [book:13-wordbook-save]
static void save()
{
    std::string blob;
    for (const auto& w : s_words) { blob += w; blob += '\n'; }
    if (sdcard_mounted()) {
        FILE* fp = fopen(SD_PATH, "wb");
        if (fp) { fwrite(blob.data(), 1, blob.size(), fp); fclose(fp); return; }
        ESP_LOGW(TAG, "cannot write %s, using NVS", SD_PATH);
    }
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) return;
    if (blob.size() > NVS_MAX) blob.resize(blob.rfind('\n', NVS_MAX) + 1);
    nvs_set_blob(h, NVS_KEY, blob.data(), blob.size());
    nvs_commit(h);
    nvs_close(h);
}
// [/book:13-wordbook-save]

static void parse(const std::string& blob)
{
    s_words.clear();
    size_t pos = 0;
    while (pos < blob.size()) {
        size_t nl = blob.find('\n', pos);
        std::string w = blob.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos);
        if (!w.empty() && w.back() == '\r') w.pop_back();
        if (!w.empty() && std::find(s_words.begin(), s_words.end(), w) == s_words.end()) s_words.push_back(w);
        if (nl == std::string::npos) break;
        pos = nl + 1;
    }
}

// [book:13-wordbook-load]
void wordbook::load()
{
    std::string blob;
    if (sdcard_mounted()) {
        FILE* fp = fopen(SD_PATH, "rb");
        if (fp) {
            char buf[512];
            size_t n;
            while ((n = fread(buf, 1, sizeof(buf), fp)) > 0) blob.append(buf, n);
            fclose(fp);
        }
    }
    if (blob.empty()) {
        nvs_handle_t h;
        if (nvs_open(NVS_NS, NVS_READONLY, &h) == ESP_OK) {
            size_t len = 0;
            if (nvs_get_blob(h, NVS_KEY, nullptr, &len) == ESP_OK && len > 0) {
                blob.resize(len);
                nvs_get_blob(h, NVS_KEY, &blob[0], &len);
            }
            nvs_close(h);
        }
    }
    parse(blob);
    ESP_LOGI(TAG, "%u words", (unsigned)s_words.size());
}
// [/book:13-wordbook-load]

bool wordbook::contains(const std::string& w) { return std::find(s_words.begin(), s_words.end(), w) != s_words.end(); }

bool wordbook::add(const std::string& w)
{
    if (w.empty() || contains(w)) return false;
    s_words.push_back(w);
    save();
    return true;
}

bool wordbook::remove(const std::string& w)
{
    auto it = std::find(s_words.begin(), s_words.end(), w);
    if (it == s_words.end()) return false;
    s_words.erase(it);
    save();
    return true;
}

bool wordbook::toggle(const std::string& w)
{
    if (contains(w)) { remove(w); return false; }
    add(w);
    return true;
}

const std::vector<std::string>& wordbook::words() { return s_words; }
