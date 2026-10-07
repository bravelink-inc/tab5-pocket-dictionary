#include "font_store.hpp"
#include <cstring>
#include "esp_log.h"
#include "esp_partition.h"
#include "app_config.hpp"

namespace {
const char* TAG = "font";

struct Slot {
    int size;
    const lgfx::IFont* builtin;
    lgfx::U8g2font* flash;       // nullptr until found in the "font" partition
};
Slot g_slots[] = {
    {20, &fonts::lgfxJapanGothic_20, nullptr},
    {24, &fonts::lgfxJapanGothic_24, nullptr},
    {28, &fonts::lgfxJapanGothic_28, nullptr},
    {40, &fonts::lgfxJapanGothic_40, nullptr},
};
constexpr uint32_t MAX_FONTS = 8;
constexpr uint32_t U8G2_HEADER = 23;   // bytes before the first glyph in a u8g2 font
}

// [book:font-load]
int font_store::load()
{
    const esp_partition_t* part = esp_partition_find_first(ESP_PARTITION_TYPE_ANY, ESP_PARTITION_SUBTYPE_ANY,
                                                           cfg::FONT_PARTITION_LABEL);
    if (!part) { ESP_LOGW(TAG, "no '%s' partition, using the built-in font", cfg::FONT_PARTITION_LABEL); return 0; }

    // header: "PFN1", count, then count x {pixel size, offset, length} (see tools/make_font.py)
    uint32_t hdr[2 + 3 * MAX_FONTS] = {};
    if (esp_partition_read(part, 0, hdr, sizeof(hdr)) != ESP_OK || memcmp(hdr, "PFN1", 4) != 0) {
        ESP_LOGW(TAG, "font partition is empty (run `idf.py flash`), using the built-in font");
        return 0;
    }
    uint32_t count = hdr[1] < MAX_FONTS ? hdr[1] : MAX_FONTS;
    uint32_t total = 0;
    for (uint32_t i = 0; i < count; ++i) {
        uint32_t end = hdr[3 + 3 * i] + hdr[4 + 3 * i];
        if (end > total) total = end;
    }
    if (total == 0 || total > part->size) { ESP_LOGW(TAG, "font image size %lu invalid", (unsigned long)total); return 0; }

    const void* ptr = nullptr;
    esp_partition_mmap_handle_t handle;
    if (esp_partition_mmap(part, 0, total, ESP_PARTITION_MMAP_DATA, &ptr, &handle) != ESP_OK) {
        ESP_LOGE(TAG, "mmap failed, using the built-in font");
        return 0;
    }
    const uint8_t* base = static_cast<const uint8_t*>(ptr);
    int found = 0;
    for (uint32_t i = 0; i < count; ++i) {
        uint32_t size = hdr[2 + 3 * i], off = hdr[3 + 3 * i], len = hdr[4 + 3 * i];
        for (auto& s : g_slots) {
            // byte 10 of a u8g2 font is its line height; it must match the size we expect
            if ((int)size == s.size && len > U8G2_HEADER && base[off + 10] == size) {
                s.flash = new lgfx::U8g2font(base + off);
                ++found;
            }
        }
    }
    ESP_LOGI(TAG, "flash fonts: %d sizes, %lu KB", found, (unsigned long)(total / 1024));
    return found;
}
// [/book:font-load]

const lgfx::IFont* font_store::get(int size)
{
    for (auto& s : g_slots)
        if (s.size == size) return s.flash ? s.flash : s.builtin;
    return &fonts::lgfxJapanGothic_24;
}
