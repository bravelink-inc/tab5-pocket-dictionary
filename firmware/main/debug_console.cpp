#include "debug_console.hpp"

#include <atomic>
#include <cerrno>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <M5Unified.h>
#include "driver/usb_serial_jtag.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_vfs_dev.h"
#include "esp_vfs_usb_serial_jtag.h"
#include "mbedtls/base64.h"
#include "mbedtls/sha256.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "keyboard.hpp"
#include "dict_store.hpp"
#include "file_replace.hpp"
#include <dirent.h>
#include <sys/stat.h>
#include <string>
#include <unistd.h>
#include "esp_heap_caps.h"

static const char* TAG = "dbg";
static std::atomic<bool> s_shot{false};

namespace {

void inject(uint8_t keycode, uint8_t modifier = 0)
{
    keyboard::post(KeyEvent{KeySource::USB, true, modifier, keycode});
    keyboard::post(KeyEvent{KeySource::USB, false, 0, keycode});
}

// [book:15-inject-char]
// Minimal ASCII/VT100 -> HID usage translation (US layout).
void injectChar(char c)
{
    if (c >= 'a' && c <= 'z') return inject(0x04 + (c - 'a'));
    if (c >= 'A' && c <= 'Z') return inject(0x04 + (c - 'A'), hid::MOD_SHIFT & 0x02);
    if (c >= '1' && c <= '9') return inject(0x1E + (c - '1'));
    switch (c) {
        case '0': return inject(0x27);
        case ' ': return inject(hid::KEY_SPACE);
        case '\r': case '\n': return inject(hid::KEY_ENTER);
        case '\t': return inject(hid::KEY_TAB);
        case 0x08: case 0x7F: return inject(hid::KEY_BACKSPACE);
        case '-': return inject(0x2D);
        case '\'': return inject(0x34);
        case '.': return inject(0x37);
        case 0x15: return inject(0x18, 0x01);   // Ctrl+U
        case 0x12: return inject(0x15, 0x01);   // Ctrl+R
        case 0x14: return inject(0x17, 0x01);   // Ctrl+T (quiz)
        case 0x11: return inject(0x14, 0x01);   // Ctrl+Q (power off)
        case 0x0C: return inject(0x0F, 0x01);   // Ctrl+L (sources and licences)
        default: break;
    }
}
// [/book:15-inject-char]

// ---- command mode (line starts with 0x01) ------------------------------------
//   PROTO               "PDICT-PUT 2 SHA256" (check before sending to an older firmware)
//   PUT <path> <size> <sha256> then base64 lines; ends with "DONE <total> <sha256>"
//   LS <dir>            lists entries, ends with "END"
//   RM <path>           deletes a file
//   RELOAD              re-scans the dictionaries
//   FREE                heap statistics
constexpr size_t LINE_CAP = 16384;

int readLine(char* buf, size_t max, uint32_t timeout_ms)
{
    size_t n = 0;
    int64_t deadline = esp_timer_get_time() + (int64_t)timeout_ms * 1000;
    while (n < max - 1) {
        uint8_t c;
        int got = usb_serial_jtag_read_bytes(&c, 1, pdMS_TO_TICKS(50));
        if (got <= 0) {
            if (esp_timer_get_time() > deadline) return -1;
            continue;
        }
        if (c == '\n') break;
        if (c == '\r') continue;
        buf[n++] = (char)c;
    }
    buf[n] = 0;
    return (int)n;
}

void ensureParentDir(const std::string& path)
{
    size_t p = path.rfind('/');
    if (p == std::string::npos || p == 0) return;
    std::string dir = path.substr(0, p);
    struct stat st;
    if (stat(dir.c_str(), &st) != 0) mkdir(dir.c_str(), 0775);
}

bool sha256File(const std::string& path, uint8_t* buf, unsigned long size, std::string& hex)
{
    FILE* fp = fopen(path.c_str(), "rb");
    if (!fp) return false;
    mbedtls_sha256_context ctx;
    mbedtls_sha256_init(&ctx);
    bool ok = mbedtls_sha256_starts(&ctx, 0) == 0;
    size_t n; unsigned long total = 0;
    while (ok && (n = fread(buf, 1, LINE_CAP, fp)) > 0) {
        ok = mbedtls_sha256_update(&ctx, buf, n) == 0;
        total += n;
    }
    unsigned char digest[32];
    ok = ok && !ferror(fp) && total == size && mbedtls_sha256_finish(&ctx, digest) == 0;
    if (fclose(fp) != 0) ok = false;
    mbedtls_sha256_free(&ctx);
    if (!ok) return false;
    char part[3]; hex.clear();
    for (unsigned char c : digest) { snprintf(part, sizeof(part), "%02x", c); hex += part; }
    return true;
}

bool parsePut(const char* args, std::string& path, unsigned long& size, std::string& hash)
{
    std::string text = args;
    size_t at = text.rfind(' ');
    if (at == std::string::npos) return false;
    hash = text.substr(at + 1); text.resize(at);
    if (hash.size() != 64 || hash.find_first_not_of("0123456789abcdef") != std::string::npos) return false;
    at = text.rfind(' ');
    if (at == std::string::npos) return false;
    const std::string number = text.substr(at + 1);
    if (number.empty() || number.find_first_not_of("0123456789") != std::string::npos) return false;
    errno = 0; char* end;
    size = strtoul(number.c_str(), &end, 10);
    if (errno || *end || size > 256UL * 1024 * 1024) return false;
    path = text.substr(0, at);
    if (path.size() > 160 || path.compare(0, 8, "/sdcard/") != 0 || path.back() == '/') return false;
    size_t start = 8;
    while (start < path.size()) {
        size_t slash = path.find('/', start);
        std::string name = path.substr(start, slash == std::string::npos ? slash : slash - start);
        if (name.empty() || name == "." || name == ".." || name.find(".pdict-") != std::string::npos ||
            name.find_first_of("\r\n\\") != std::string::npos) return false;
        for (unsigned char c : name) if (c < 32) return false;
        if (slash == std::string::npos) break;
        start = slash + 1;
    }
    return true;
}

// [book:15-cmd-put]
// [book:15-cmd-put-start]
void cmdPut(char* args, char* line, uint8_t* bin)
{
    std::string path, expected;
    unsigned long size = 0;
    if (!parsePut(args, path, size, expected)) { printf("ERR usage: PUT <SD path> <size> <sha256>\n"); return; }
    struct Resume {
        ~Resume() { dict_store::endTransferPause(); }
    } resume;
    dict_store::requestTransferPause();
    const int64_t deadline = esp_timer_get_time() + 30000000;
    while (!dict_store::transferPaused()) {
        if (esp_timer_get_time() > deadline) { printf("ERR UI busy\n"); return; }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    ensureParentDir(path);
    if (!file_replace::recover(path)) { printf("ERR recovery\n"); return; }
    const std::string temp = file_replace::temporary(path);
    FILE* fp = fopen(temp.c_str(), "wb");
// [/book:15-cmd-put-start]
    if (!fp) { printf("ERR cannot open staging file\n"); return; }
    setvbuf(fp, nullptr, _IOFBF, 32768);
    printf("READY\n"); fflush(stdout);
    unsigned long total = 0;
    bool ok = true;
    while (total < size) {
        int n = readLine(line, LINE_CAP, 15000);
        if (n < 0) { printf("ERR timeout\n"); ok = false; break; }
        if (n == 0) continue;
        if (strcmp(line, "ABORT") == 0) { printf("ERR aborted\n"); ok = false; break; }
        size_t olen = 0;
        if (mbedtls_base64_decode(bin, LINE_CAP, &olen, (const unsigned char*)line, n) != 0) { printf("ERR base64\n"); ok = false; break; }
        if (!olen || olen > size - total) { printf("ERR size\n"); ok = false; break; }
        if (fwrite(bin, 1, olen, fp) != olen) { printf("ERR write\n"); ok = false; break; }
// [book:15-cmd-put-end]
        total += olen;
        printf("OK %lu\n", total); fflush(stdout);
    }
    if (fflush(fp) != 0 || fsync(fileno(fp)) != 0) ok = false;
    if (fclose(fp) != 0) ok = false;
    std::string actual;
    if (ok) ok = sha256File(temp, bin, size, actual) && actual == expected;
    if (ok) ok = file_replace::commit(path);
    if (ok) printf("DONE %lu %s\n", total, actual.c_str());
    else { unlink(temp.c_str()); printf("ERR transfer not committed\n"); }
    fflush(stdout);
}
// [/book:15-cmd-put-end]
// [/book:15-cmd-put]

void cmdLs(const char* dir)
{
    DIR* d = opendir(*dir ? dir : "/sdcard");
    if (!d) { printf("ERR cannot open %s\n", dir); return; }
    while (dirent* e = readdir(d)) {
        std::string p = std::string(*dir ? dir : "/sdcard") + "/" + e->d_name;
        struct stat st;
        long sz = (stat(p.c_str(), &st) == 0) ? (long)st.st_size : -1;
        printf("%c %10ld %s\n", e->d_type == DT_DIR ? 'd' : '-', sz, e->d_name);
    }
    closedir(d);
    printf("END\n"); fflush(stdout);
}

void handleCommand(char* line, char* linebuf, uint8_t* bin)
{
    if (strcmp(line, "PROTO") == 0) printf("PDICT-PUT 2 SHA256\n");
    else if (strncmp(line, "PUT ", 4) == 0) cmdPut(line + 4, linebuf, bin);
    else if (strncmp(line, "LS", 2) == 0) cmdLs(line[2] == ' ' ? line + 3 : "");
    else if (strncmp(line, "RM ", 3) == 0) { printf(unlink(line + 3) == 0 ? "OK\n" : "ERR\n"); }
    else if (strcmp(line, "RELOAD") == 0) { dict_store::requestReload(); printf("OK\n"); }
    else if (strcmp(line, "FREE") == 0) {
        printf("internal free %u largest %u | psram free %u largest %u\n",
               (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL), (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
               (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM), (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM));
    }
    else printf("ERR unknown command\n");
    fflush(stdout);
}

void task(void*)
{
    usb_serial_jtag_driver_config_t cfg = USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
    cfg.rx_buffer_size = 8192;
    cfg.tx_buffer_size = 8192;
    char* linebuf = static_cast<char*>(heap_caps_malloc(LINE_CAP, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    uint8_t* bin = static_cast<uint8_t*>(heap_caps_malloc(LINE_CAP, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    char cmd[256];
    int cmdLen = -1;   // >= 0 while a command line (after 0x01) is being collected
    if (usb_serial_jtag_driver_install(&cfg) == ESP_OK) {
        esp_vfs_usb_serial_jtag_use_driver();
    }
    ESP_LOGI(TAG, "debug console ready: type to search, 'S' = screenshot, arrows/PgUp/PgDn supported");

    uint8_t buf[16];
    char esc[8]; int escLen = 0;
    for (;;) {
        int n = usb_serial_jtag_read_bytes(buf, sizeof(buf), pdMS_TO_TICKS(100));
        for (int i = 0; i < n; ++i) {
            const char c = static_cast<char>(buf[i]);
            if (cmdLen >= 0) {   // collecting a command line
                if (c == '\n') {
                    cmd[cmdLen] = 0;
                    if (linebuf && bin) handleCommand(cmd, linebuf, bin);
                    cmdLen = -1;
                } else if (c != '\r' && cmdLen < (int)sizeof(cmd) - 1) {
                    cmd[cmdLen++] = c;
                }
                continue;
            }
            if (escLen > 0) {
                esc[escLen++] = c;
                if (escLen == 2 && c != '[') { escLen = 0; inject(hid::KEY_ESC); injectChar(c); continue; }
                if (escLen >= 3) {
                    if (c == 'A') inject(hid::KEY_UP);
                    else if (c == 'B') inject(hid::KEY_DOWN);
                    else if (c == 'C') inject(hid::KEY_RIGHT);
                    else if (c == 'D') inject(hid::KEY_LEFT);
                    else if (c == 'H') inject(hid::KEY_HOME);
                    else if (c == 'F') inject(hid::KEY_END);
                    else if (c == '~') { if (esc[2] == '5') inject(hid::KEY_PAGEUP); else if (esc[2] == '6') inject(hid::KEY_PAGEDOWN); }
                    else if (c >= '0' && c <= '9' && escLen < 7) continue;
                    escLen = 0;
                }
                continue;
            }
            if (c == 0x1B) { esc[0] = c; escLen = 1; continue; }
            if (c == 0x01) { cmdLen = 0; continue; }   // command line follows
            if (c == 'S') { s_shot = true; continue; }
            injectChar(c);
        }
        if (escLen == 1 && n == 0) { escLen = 0; inject(hid::KEY_ESC); }   // lone ESC
    }
}

}  // namespace

void debug_console_start()
{
    xTaskCreatePinnedToCore(task, "dbg_console", 8192, nullptr, 3, nullptr, 0);
}

bool debug_screenshot_pending() { return s_shot.exchange(false); }

// [book:15-dump-screen]
// Dump the whole frame buffer as base64 RGB565 rows: "PDSHOT <w> <h>" ... "PDSHOT END".
void debug_dump_screen()
{
    auto& d = M5.Display;
    const int w = d.width(), h = d.height();
    static uint16_t* row = nullptr;
    static unsigned char* b64 = nullptr;
    if (!row) row = static_cast<uint16_t*>(heap_caps_malloc(w * 2, MALLOC_CAP_8BIT));
    if (!b64) b64 = static_cast<unsigned char*>(heap_caps_malloc(w * 3 + 8, MALLOC_CAP_8BIT));
    if (!row || !b64) return;
    esp_log_level_set("*", ESP_LOG_NONE);   // keep log lines out of the dump
    printf("\nPDSHOT %d %d\n", w, h);
    for (int y = 0; y < h; ++y) {
        d.readRect(0, y, w, 1, row);
        size_t olen = 0;
        mbedtls_base64_encode(b64, w * 3 + 8, &olen, reinterpret_cast<const unsigned char*>(row), w * 2);
        b64[olen] = 0;
        printf("R%d:%s\n", y, b64);
        if ((y & 7) == 0) vTaskDelay(1);   // give the USB driver time to drain (fewer lost rows)
    }
    printf("PDSHOT END\n");
    fflush(stdout);
    esp_log_level_set("*", ESP_LOG_INFO);
}
// [/book:15-dump-screen]
