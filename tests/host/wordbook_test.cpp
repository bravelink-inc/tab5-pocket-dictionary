#include "wordbook.hpp"
#include "file_replace.hpp"
#include "nvs.h"
#include "settings.hpp"
#include <cassert>
#include <cerrno>
#include <csignal>
#include <cstring>
#include <fstream>
#include <iterator>
#include <fcntl.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <unistd.h>

static bool mounted = true, writeFailure = false, commitFailure = false, renameFailure = false;
static std::string durable, pending, target;
static int initError = 0, eraseCalls = 0;
int nvs_flash_init() { return initError; }
int nvs_flash_erase() { ++eraseCalls; durable.clear(); return ESP_OK; }
int nvs_get_i8(int, const char*, int8_t*) { return ESP_ERR_NVS_NOT_FOUND; }
int nvs_set_i8(int, const char*, int8_t) { return ESP_OK; }
bool sdcard_mounted() { return mounted; }
int nvs_open(const char*, int, nvs_handle_t* h) { *h = 1; return ESP_OK; }
int nvs_get_blob(int, const char*, void* data, size_t* len) {
    if (!data) { *len = durable.size(); return ESP_OK; }
    if (*len < durable.size()) return ESP_ERR_INVALID_SIZE;
    memcpy(data, durable.data(), durable.size()); *len = durable.size(); return ESP_OK;
}
int nvs_set_blob(int, const char*, const void* data, size_t len) {
    if (writeFailure) return 3;
    pending.assign(static_cast<const char*>(data), len); return ESP_OK;
}
int nvs_erase_key(int, const char*) { pending.clear(); return ESP_OK; }
int nvs_commit(int) { if (commitFailure) return 4; durable = pending; return ESP_OK; }
void nvs_close(int) {}
extern "C" int rename(const char* from, const char* to) {
    if (renameFailure && from == file_replace::temporary(target)) { errno = EIO; return -1; }
    return renameat(AT_FDCWD, from, AT_FDCWD, to);
}
static void write(const std::string& path, const std::string& text) { std::ofstream(path, std::ios::binary) << text; }
static std::string read(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

int main(int argc, char** argv) {
    assert(argc == 3); target = argv[1]; const std::string mode = argv[2];
    durable = "stale\n";
    if (mode == "empty") {
        write(target, ""); wordbook::load(); assert(wordbook::entries().empty());
    } else if (mode == "legacy") {
        write(target, "Color,colour\r\n心【こころ】\nColor,colour\n");
        wordbook::load(); assert(wordbook::entries().size() == 2);
        assert(wordbook::contains("Color,colour") && wordbook::contains("心【こころ】"));
        assert(wordbook::add("organize,organise"));
        wordbook::load(); assert(wordbook::contains("organize,organise"));
        assert(wordbook::toggle("organize,organise") == false);
        assert(!wordbook::contains("organize,organise"));
    } else if (mode == "two-column") {
        const std::string old = "Color,colour\tcolour\r\n心【こころ】\t\nColor,colour\tcolor\n";
        write(target, old); wordbook::load();
        assert(wordbook::entries().size() == 2);
        assert(read(target) == old); // Loading alone never rewrites the user's file.
        assert(wordbook::contains("Color,colour") && wordbook::contains("心【こころ】"));
        assert(wordbook::add("東京"));
        assert(read(target) == "Color,colour\n心【こころ】\n東京\n");
        wordbook::load(); assert(wordbook::entries().size() == 3);
        assert(!wordbook::toggle("Color,colour"));
        assert(wordbook::contains("心【こころ】") && wordbook::contains("東京"));
    } else if (mode == "nvs-two-column") {
        mounted = false; durable = "apple\tapple\n心\t\n";
        wordbook::load(); assert(wordbook::entries().size() == 2);
        assert(durable == "apple\tapple\n心\t\n");
        assert(wordbook::add("東京"));
        assert(durable == "apple\n心\n東京\n");
        wordbook::load(); assert(wordbook::contains("心") && wordbook::contains("東京"));
    } else if (mode == "failure" || mode == "short-write") {
        write(target, "first\n"); wordbook::load();
        if (mode == "failure") mkdir(file_replace::temporary(target).c_str(), 0700);
        else { signal(SIGXFSZ, SIG_IGN); rlimit lim{16, 16}; assert(setrlimit(RLIMIT_FSIZE, &lim) == 0); }
        assert(!wordbook::add(std::string(100, 'x')));
        assert(!wordbook::lastError().empty());
        assert(wordbook::entries().size() == 1 && read(target) == "first\n");
    } else if (mode == "nvs") {
        mounted = false; wordbook::load(); assert(wordbook::contains("stale"));
        writeFailure = true; assert(!wordbook::add("failed")); writeFailure = false;
        commitFailure = true; assert(!wordbook::remove("stale")); commitFailure = false;
        assert(wordbook::contains("stale") && durable == "stale\n");
        for (int i = 0; i < 7; ++i) assert(wordbook::add(std::to_string(i) + std::string(500, 'x')));
        const auto size = wordbook::entries().size();
        assert(!wordbook::add("oversize" + std::string(500, 'x')));
        assert(wordbook::entries().size() == size && !wordbook::lastError().empty());
    } else if (mode == "replace") {
        write(target, "old"); write(file_replace::temporary(target), "new");
        renameFailure = true; assert(!file_replace::commit(target)); renameFailure = false;
        assert(read(target) == "old");
        assert(file_replace::commit(target) && read(target) == "new");
        assert(rename(target.c_str(), (target + ".pdict-bak").c_str()) == 0);
        assert(file_replace::recover(target) && read(target) == "new");
        write(target + ".pdict-bak", "previous");
        assert(file_replace::recover(target) && read(target) == "new");
    } else if (mode == "oversize") {
        write(target, "first\n"); wordbook::load();
        write(target, std::string(65537, 'x')); wordbook::load();
        assert(wordbook::contains("first") && !wordbook::lastError().empty());
        assert(!wordbook::add("valid"));
        assert(read(target) == std::string(65537, 'x'));
        assert(!wordbook::add("bad\nline") && !wordbook::add(std::string(513, 'x')));
    } else if (mode == "init") {
        for (int error : {5, 6}) {
            initError = error; settings_init();
            assert(eraseCalls == 0 && durable == "stale\n");
        }
    } else { assert(false); }
}
