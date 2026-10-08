#include "dictionary.hpp"

#include <cstring>
#include <cctype>
#include <algorithm>
#include "esp_heap_caps.h"
#include "esp_log.h"

static const char* TAG = "dict";
static constexpr size_t MAX_RECORD = 512 * 1024;
static constexpr size_t MAX_RESIDENT_DEFS = 4 * 1024 * 1024;

Dictionary::~Dictionary() { close(); }

void Dictionary::close()
{
    if (_fp) { fclose(_fp); _fp = nullptr; }
    if (_owned_index) { heap_caps_free(_owned_index); _owned_index = nullptr; }
    if (_owned_defs)  { heap_caps_free(_owned_defs);  _owned_defs  = nullptr; }
    _keys = nullptr; _kidx = nullptr; _dref = nullptr; _defs = nullptr;
    _count = 0; _defs_size = 0; _defs_off = 0;
    _title.clear(); _tag.clear(); _source.clear();
}

static uint32_t rd32(const uint8_t* p) { uint32_t v; memcpy(&v, p, 4); return v; }

// [book:8-parse-header]
bool Dictionary::parseHeader(const uint8_t* raw, size_t avail, Header& h)
{
    if (!raw || avail < 64 || memcmp(raw, "PDC1", 4) != 0) return false;
    h.version    = rd32(raw + 4);
    h.count      = rd32(raw + 8);
    h.flags      = rd32(raw + 12);
    h.keys_off   = rd32(raw + 16);
    h.keys_size  = rd32(raw + 20);
    h.kidx_off   = rd32(raw + 24);
    h.dref_off   = rd32(raw + 28);
    h.defs_off   = rd32(raw + 32);
    h.defs_size  = rd32(raw + 36);
    h.title_off  = rd32(raw + 40);
    h.total_size = rd32(raw + 44);
    if (h.version != 1 || h.flags != 0 || h.count == 0 || h.count > 20000000u) return false;
    // Use 64-bit arithmetic before comparing offsets: a corrupt size must not wrap.
    if (h.keys_off != 64 || !h.keys_size || (h.keys_size & 3) || h.kidx_off != 64ull + h.keys_size) return false;
    if (h.dref_off != h.kidx_off + 4ull * h.count || h.defs_off != h.dref_off + 4ull * h.count) return false;
    if (h.defs_size < 6 || h.title_off != uint64_t(h.defs_off) + h.defs_size || h.total_size <= h.title_off) return false;
    return true;
}
// [/book:8-parse-header]

// build_dict.py stores each key consecutively in sorted order. Validate that layout
// once before binary search can dereference a key. Definition contents are checked
// when read, including when they are kept on SD rather than loaded into RAM.
bool Dictionary::validateIndex(const Header& h, const uint8_t* index)
{
    const char* keys = reinterpret_cast<const char*>(index);
    const uint8_t* kidx = index + (h.kidx_off - h.keys_off);
    const uint8_t* dref = index + (h.dref_off - h.keys_off);
    size_t next = 0;
    const char* previous = nullptr;
    for (uint32_t i = 0; i < h.count; ++i) {
        const uint32_t off = rd32(kidx + 4 * size_t(i));
        if (off != next || off >= h.keys_size) return false;
        const char* key = keys + off;
        const char* end = static_cast<const char*>(memchr(key, 0, h.keys_size - off));
        if (!end || end == key || (previous && strcmp(previous, key) > 0)) return false;
        const uint32_t ref = rd32(dref + 4 * size_t(i));
        if ((ref & 3) || ref > h.defs_size - 6) return false;
        previous = key;
        next = size_t(end - keys) + 1;
    }
    // Only alignment padding may follow the last key.
    if (h.keys_size - next > 3) return false;
    while (next < h.keys_size) if (keys[next++] != 0) return false;
    return true;
}

std::string Dictionary::readTag(const uint8_t* raw)
{
    const char* t = reinterpret_cast<const char*>(raw + 48);
    return std::string(t, strnlen(t, 16));
}

// Title, with a fallback tag derived from its first two characters.
void Dictionary::setTitle(const char* title, size_t maxlen)
{
    _title.assign(title, strnlen(title, maxlen));
    if (_tag.empty()) {
        size_t n = 0;
        for (int chars = 0; n < _title.size() && chars < 2; ++chars) {
            unsigned char c = _title[n];
            n += c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : 4;
        }
        _tag = _title.substr(0, std::min(n, _title.size()));
    }
}

void Dictionary::attach(const Header& h, const uint8_t* index_base, const uint8_t* defs_base)
{
    _keys = reinterpret_cast<const char*>(index_base);
    _kidx = reinterpret_cast<const uint32_t*>(index_base + (h.kidx_off - h.keys_off));
    _dref = reinterpret_cast<const uint32_t*>(index_base + (h.dref_off - h.keys_off));
    _defs = defs_base;
    _defs_size = h.defs_size;
    _defs_off = h.defs_off;
    _count = h.count;
}

// [book:8-open-memory]
bool Dictionary::openMemory(const uint8_t* base, size_t size, const std::string& source)
{
    close();
    Header h;
    if (!parseHeader(base, size, h) || h.total_size > size ||
        !validateIndex(h, base + h.keys_off) ||
        !memchr(base + h.title_off, 0, h.total_size - h.title_off)) {
        ESP_LOGE(TAG, "%s: bad header", source.c_str());
        return false;
    }
    attach(h, base + h.keys_off, base + h.defs_off);
    _tag = readTag(base);
    setTitle(reinterpret_cast<const char*>(base + h.title_off), h.total_size - h.title_off);
    _source = source;
    ESP_LOGI(TAG, "opened '%s' from %s: %lu entries (memory)", _title.c_str(), source.c_str(), (unsigned long)_count);
    return true;
}
// [/book:8-open-memory]

// [book:11-open-file]
bool Dictionary::openFile(const char* path)
{
    close();
    FILE* fp = fopen(path, "rb");
    if (!fp) { ESP_LOGE(TAG, "cannot open %s", path); return false; }

    uint8_t raw[64];
    Header h;
    if (fread(raw, 1, 64, fp) != 64 || !parseHeader(raw, 64, h)) {
        ESP_LOGE(TAG, "%s: bad header", path);
        fclose(fp);
        return false;
    }

    if (fseek(fp, 0, SEEK_END) != 0) { fclose(fp); return false; }
    const long actual_size = ftell(fp);
    if (actual_size < 0 || uint64_t(actual_size) < h.total_size) {
        ESP_LOGE(TAG, "%s: truncated image", path);
        fclose(fp);
        return false;
    }

    const size_t index_bytes = h.defs_off - h.keys_off;
    uint8_t* idx = static_cast<uint8_t*>(heap_caps_malloc(index_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!idx) { ESP_LOGE(TAG, "%s: no memory for index (%u bytes)", path, (unsigned)index_bytes); fclose(fp); return false; }
    if (fseek(fp, h.keys_off, SEEK_SET) != 0 || fread(idx, 1, index_bytes, fp) != index_bytes) {
        ESP_LOGE(TAG, "%s: short read (index)", path);
        heap_caps_free(idx); fclose(fp);
        return false;
    }
    if (!validateIndex(h, idx)) {
        ESP_LOGE(TAG, "%s: bad index", path);
        heap_caps_free(idx); fclose(fp);
        return false;
    }

    char tbuf[128] = {0};
    if (fseek(fp, h.title_off, SEEK_SET) == 0) {
        size_t n = fread(tbuf, 1, std::min<size_t>(sizeof(tbuf), h.total_size - h.title_off), fp);
        if (!memchr(tbuf, 0, n)) {
            ESP_LOGE(TAG, "%s: unterminated or overlong title", path);
            heap_caps_free(idx); fclose(fp);
            return false;
        }
    } else {
        heap_caps_free(idx); fclose(fp);
        return false;
    }

    // Small dictionaries keep their definitions in PSRAM; big ones are read on demand
    // so that several large dictionaries can coexist (only their key indexes stay in RAM).
    uint8_t* defs = nullptr;
    const size_t largest = heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM);
    if (h.defs_size <= MAX_RESIDENT_DEFS && h.defs_size + (6u << 20) < largest) {
        defs = static_cast<uint8_t*>(heap_caps_malloc(h.defs_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        if (defs && (fseek(fp, h.defs_off, SEEK_SET) != 0 || fread(defs, 1, h.defs_size, fp) != h.defs_size)) {
            ESP_LOGW(TAG, "%s: short read (defs), falling back to lazy reads", path);
            heap_caps_free(defs);
            defs = nullptr;
        }
    }

    attach(h, idx, defs);
    _owned_index = idx;
    _owned_defs = defs;
    _tag = readTag(raw);
    setTitle(tbuf, sizeof(tbuf));
    _source = path;
    if (defs) {
        fclose(fp);
    } else {
        _fp = fp;
    }
    ESP_LOGI(TAG, "opened '%s' [%s] from %s: %lu entries, index %u KB (%s)", _title.c_str(), _tag.c_str(), path,
             (unsigned long)_count, (unsigned)(index_bytes >> 10), defs ? "psram" : "lazy");
    return true;
}
// [/book:11-open-file]

// [book:8-lower-bound]
uint32_t Dictionary::lowerBound(const char* prefix) const
{
    uint32_t lo = 0, hi = _count;
    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2;
        if (strcmp(key(mid), prefix) < 0) lo = mid + 1; else hi = mid;
    }
    return lo;
}
// [/book:8-lower-bound]

bool Dictionary::hasPrefix(uint32_t i, const char* prefix) const
{
    return i < _count && strncmp(key(i), prefix, strlen(prefix)) == 0;
}

bool Dictionary::parseRecord(const uint8_t* rec, size_t avail, std::string& head, std::string& def)
{
    if (avail < 4) return false;
    uint32_t len = rd32(rec);
    if (len < 2 || len > MAX_RECORD || len > avail - 4) return false;
    const char* p = reinterpret_cast<const char*>(rec + 4);
    size_t hl = strnlen(p, len);
    if (hl + 1 >= len) return false;
    const char* d = p + hl + 1;
    const size_t dl = strnlen(d, len - hl - 1);
    if (dl + 1 != len - hl - 1) return false;
    head.assign(p, hl);
    def.assign(d, dl);
    return true;
}

// [book:8-entry]
bool Dictionary::entry(uint32_t i, std::string& headword, std::string& definition) const
{
    if (i >= _count) return false;
    const uint32_t off = _dref[i];
    if (_defs_size < 4 || off > _defs_size - 4) return false;
    if (_defs) {
        if (off >= _defs_size) return false;
        return parseRecord(_defs + off, _defs_size - off, headword, definition);
    }
    if (!_fp) return false;
    uint8_t lenbuf[4];
    if (fseek(_fp, uint64_t(_defs_off) + off, SEEK_SET) != 0 || fread(lenbuf, 1, 4, _fp) != 4) return false;
    uint32_t len = rd32(lenbuf);
    if (len < 2 || len > MAX_RECORD || len > _defs_size - off - 4) return false;
    std::string buf(len + 4, '\0');
    memcpy(&buf[0], lenbuf, 4);
    if (fread(&buf[4], 1, len, _fp) != len) return false;
    return parseRecord(reinterpret_cast<const uint8_t*>(buf.data()), buf.size(), headword, definition);
}
// [/book:8-entry]

// [book:8-normalize]
std::string Dictionary::normalize(const std::string& text)
{
    std::string out;
    out.reserve(text.size());
    bool pending_space = false;
    for (unsigned char c : text) {
        if (c == ' ' || c == '\t') { pending_space = !out.empty(); continue; }
        if (pending_space) { out.push_back(' '); pending_space = false; }
        out.push_back(static_cast<char>(c < 0x80 ? tolower(c) : c));
    }
    return out;
}
// [/book:8-normalize]
