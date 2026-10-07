// Driver for the M5Stack Tab5 Keyboard (STM32F030 based, I2C address 0x6D).
//
// The keyboard is switched into "HID mode" so every key press arrives as a
// standard HID (modifier, usage) pair, exactly like a USB keyboard.
//
// Register map (from M5Unit-KEYBOARD / datasheet):
//   0x00 INT_CFG        0x01 INT_STAT      0x02 EVENT_NUM (0-32)
//   0x10 MODE_KEYBOARD  (0 normal, 1 HID, 2 character)
//   0x30 HID_EVENT      modifier + keycode, 0xFF 0xFF when the queue is empty
//   0xFE FIRMWARE_VERSION

#include "keyboard.hpp"

#include <atomic>
#include <M5Unified.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "app_config.hpp"

static const char* TAG = "kbd_i2c";
static std::atomic<bool> s_connected{false};

namespace {
constexpr uint8_t REG_INT_CFG = 0x00, REG_EVENT_NUM = 0x02, REG_MODE = 0x10, REG_HID_EVENT = 0x30, REG_FW_VERSION = 0xFE;
constexpr uint8_t MODE_HID = 1;

bool readReg(uint8_t reg, uint8_t* buf, size_t len)
{
    return M5.Ex_I2C.readRegister(cfg::KBD_I2C_ADDR, reg, buf, len, cfg::KBD_I2C_FREQ);
}
bool writeReg(uint8_t reg, uint8_t val)
{
    return M5.Ex_I2C.writeRegister8(cfg::KBD_I2C_ADDR, reg, val, cfg::KBD_I2C_FREQ);
}

// [book:9-i2c-setup]
bool setup()
{
    uint8_t fw = 0;
    if (!readReg(REG_FW_VERSION, &fw, 1)) return false;
    if (!writeReg(REG_MODE, MODE_HID)) return false;   // also clears the previous mode's queue
    writeReg(REG_INT_CFG, 0x07);
    writeReg(REG_EVENT_NUM, 0);                        // flush + release INT
    ESP_LOGI(TAG, "Tab5 Keyboard found (firmware v%u), HID mode", fw);
    return true;
}
// [/book:9-i2c-setup]

// [book:9-i2c-drain]
// Drain the device queue. Returns false on an I2C error.
bool drain()
{
    uint8_t n = 0;
    int guard = 64;
    while (guard-- > 0) {
        if (!readReg(REG_EVENT_NUM, &n, 1)) return false;
        if (n == 0 || n == 0xFF) return true;
        uint8_t ev[2];
        if (!readReg(REG_HID_EVENT, ev, 2)) return false;
        if (ev[0] == 0xFF && ev[1] == 0xFF) return true;
        const uint8_t mod = ev[0], key = ev[1];
        if (key == 0 || (key >= 0xE0 && key <= 0xE7)) continue;   // release / modifier-only
        keyboard::post(KeyEvent{KeySource::I2C, true, mod, key});
    }
    return true;
}
// [/book:9-i2c-drain]

// [book:9-i2c-task]
void task(void*)
{
    gpio_config_t io = {};
    io.pin_bit_mask = 1ULL << cfg::KBD_INT;
    io.mode = GPIO_MODE_INPUT;
    io.pull_up_en = GPIO_PULLUP_ENABLE;
    gpio_config(&io);

    // Move the external I2C bus from Port A to Ext.Port1 (GPIO0/GPIO1).
    M5.Ex_I2C.release();
    M5.Ex_I2C.begin(static_cast<i2c_port_t>(cfg::KBD_I2C_PORT), cfg::KBD_SDA, cfg::KBD_SCL);

    int64_t last_probe = 0;
    int errors = 0, tick = 0;
    for (;;) {
        if (!s_connected) {
            const int64_t now = esp_timer_get_time();
            if (now - last_probe > 500 * 1000) {
                last_probe = now;
                if (setup()) { s_connected = true; errors = 0; }
            }
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }
        // Event-driven via INT (active low), plus a periodic safety poll.
        const bool irq = gpio_get_level(cfg::KBD_INT) == 0;
        if (irq || ++tick >= 10) {
            tick = 0;
            if (drain()) {
                errors = 0;
            } else if (++errors > 5) {
                ESP_LOGW(TAG, "keyboard lost");
                s_connected = false;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
// [/book:9-i2c-task]
}  // namespace

bool keyboard::i2cConnected() { return s_connected; }

void keyboard::startI2C()
{
    xTaskCreatePinnedToCore(task, "kbd_i2c", 4096, nullptr, 6, nullptr, 1);
}
