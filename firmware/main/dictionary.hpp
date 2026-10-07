#pragma once
#include <cstdint>
#include <cstdio>
#include <string>

// Reader for the PDC1 dictionary format produced by tools/build_dict.py.
//
// The key index (normalized keys + offsets) always lives in RAM (or memory-mapped
// flash), so prefix search is a plain binary search. Definition records are read
// from RAM when the whole file fits into PSRAM, otherwise on demand from the file.
class Dictionary {
public:
    Dictionary() = default;
    ~Dictionary();
    Dictionary(const Dictionary&) = delete;
    Dictionary& operator=(const Dictionary&) = delete;

    // Memory-backed image (e.g. memory-mapped flash partition). `base` must stay valid.
    bool openMemory(const uint8_t* base, size_t size, const std::string& source);
    // File-backed image (SD card).
    bool openFile(const char* path);
    void close();

    bool valid() const { return _count != 0; }
    uint32_t count() const { return _count; }
    const std::string& title() const { return _title; }
    const std::string& tag() const { return _tag; }      // short label for cross-dictionary results
    const std::string& source() const { return _source; }
    bool lazy() const { return _fp != nullptr; }

    // Normalized key of entry i (lowercase, sorted bytewise).
    const char* key(uint32_t i) const { return _keys + _kidx[i]; }
    // First index whose key is >= prefix.
    uint32_t lowerBound(const char* prefix) const;
    bool hasPrefix(uint32_t i, const char* prefix) const;
    // Display headword + definition of entry i.
    bool entry(uint32_t i, std::string& headword, std::string& definition) const;

    // Same normalization as build_dict.py for ASCII input (lowercase, trim, collapse spaces).
    static std::string normalize(const std::string& text);

private:
    struct Header {
        uint32_t version, count, flags;
        uint32_t keys_off, keys_size, kidx_off, dref_off, defs_off, defs_size, title_off, total_size;
    };
    static bool parseHeader(const uint8_t* raw, size_t avail, Header& h);
    static std::string readTag(const uint8_t* raw);
    void setTitle(const char* title, size_t maxlen);
    void attach(const Header& h, const uint8_t* index_base, const uint8_t* defs_base);
    static bool parseRecord(const uint8_t* rec, size_t avail, std::string& head, std::string& def);

    const char*     _keys = nullptr;
    const uint32_t* _kidx = nullptr;
    const uint32_t* _dref = nullptr;
    const uint8_t*  _defs = nullptr;
    uint32_t _count = 0, _defs_size = 0, _defs_off = 0;
    uint8_t* _owned_index = nullptr;
    uint8_t* _owned_defs  = nullptr;
    FILE*    _fp = nullptr;
    std::string _title, _tag, _source;
};
