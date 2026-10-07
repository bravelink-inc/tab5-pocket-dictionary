// M5Stack Tab5 pocket dictionary
//
//   * Tab5 Keyboard (I2C, Ext.Port1) and/or a USB keyboard on the USB-A port
//   * dictionaries: built-in EJDict-hand (flash "dict" partition) + /sdcard/dict/*.pdc

#include <memory>
#include <vector>
#include <M5Unified.h>
#include "esp_log.h"
#include "app_config.hpp"
#include "dict_store.hpp"
#include "keyboard.hpp"
#include "sdcard.hpp"
#include "settings.hpp"
#include "debug_console.hpp"
#include "ui.hpp"
#include "wordbook.hpp"
#include "font_store.hpp"

static const char* TAG = "main";

// [book:8-app-main]
extern "C" void app_main(void)
{
    auto mcfg = M5.config();
    mcfg.internal_mic = false;
    mcfg.internal_spk = false;
    mcfg.internal_imu = false;
    M5.begin(mcfg);
    settings_init();
    font_store::load();
    M5.Display.setRotation(settings_get_rotation(cfg::DISPLAY_ROTATION));
    M5.Display.setBrightness(cfg::DISPLAY_BRIGHTNESS);
    ESP_LOGI(TAG, "board=%d display=%dx%d", (int)M5.getBoard(), M5.Display.width(), M5.Display.height());
    ui::showSplash("初期化中...");

    keyboard::init();
    keyboard::startUSB();
    keyboard::startI2C();
    debug_console_start();

    ui::showSplash("SD カードをマウント中...");
    sdcard_mount();

    wordbook::load();
    ui::showSplash("辞書を読み込み中...");
    static std::vector<std::unique_ptr<Dictionary>> dicts;
    dict_store::loadAll(dicts);
    if (dicts.empty()) ESP_LOGW(TAG, "no dictionaries found (flash partition empty and no /sdcard/dict/*.pdc)");

    ui::run(dicts);
}
// [/book:8-app-main]
