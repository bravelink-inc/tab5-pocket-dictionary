// 第 4 章: Tab5 の画面に日本語を出す最小プログラム
#include <M5Unified.h>
#include "esp_log.h"

static const char* TAG = "hello";

extern "C" void app_main(void)
{
    // [book:4-begin]
    auto cfg = M5.config();
    M5.begin(cfg);                       // 画面・タッチ・電源まわりを初期化
    M5.Display.setRotation(3);           // 横向き（キーボード側が下）
    M5.Display.setBrightness(200);
    ESP_LOGI(TAG, "board=%d display=%dx%d", (int)M5.getBoard(),
             M5.Display.width(), M5.Display.height());
    // [/book:4-begin]

    // [book:4-draw]
    auto& d = M5.Display;
    d.fillScreen(0xFFFFFFu);
    d.setTextDatum(textdatum_t::middle_center);
    d.setTextColor(0x1F3B5Cu);
    d.setFont(&fonts::lgfxJapanGothic_40);          // M5GFX 内蔵の日本語フォント
    d.drawString("こんにちは、Tab5", d.width() / 2, d.height() / 2 - 40);
    d.setFont(&fonts::lgfxJapanGothic_24);
    d.setTextColor(0x7A8794u);
    d.drawString("ESP-IDF v5.5 + M5Unified で日本語が表示できました",
                 d.width() / 2, d.height() / 2 + 30);
    // [/book:4-draw]

    // [book:4-loop]
    for (;;) {
        M5.update();                     // タッチの状態を更新
        auto t = M5.Touch.getDetail();
        if (t.wasPressed()) {
            d.fillCircle(t.x, t.y, 12, 0x2F6FD6u);
            ESP_LOGI(TAG, "touch x=%d y=%d", t.x, t.y);
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    // [/book:4-loop]
}
