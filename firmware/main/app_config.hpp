#pragma once
#include <cstdint>
#include "driver/gpio.h"

// Board-level constants for the M5Stack Tab5 pocket dictionary.
namespace cfg {

// ---- Display ----------------------------------------------------------------
constexpr int     DISPLAY_ROTATION  = 3;    // landscape 1280x720, keyboard edge at the bottom (Ctrl+R flips it)
constexpr uint8_t DISPLAY_BRIGHTNESS = 200;

// ---- Tab5 Keyboard (M5Stack A164) on Ext.Port1, I2C ---------------------------
// ExtPort1 pin assignment: SDA=GPIO0, SCL=GPIO1, INT=GPIO50 (active low).
// On ESP32-P4 M5Unified uses I2C_NUM_1 for the internal bus, so the external
// bus (M5.Ex_I2C) is I2C_NUM_0; we re-point it from Port A to Ext.Port1.
constexpr int        KBD_I2C_PORT = 0;
constexpr gpio_num_t KBD_SDA      = GPIO_NUM_0;
constexpr gpio_num_t KBD_SCL      = GPIO_NUM_1;
constexpr gpio_num_t KBD_INT      = GPIO_NUM_50;
constexpr uint8_t    KBD_I2C_ADDR = 0x6D;
constexpr uint32_t   KBD_I2C_FREQ = 400000;

// ---- USB-A host port power: PI4IOE5V6408 IO expander #2 (0x44), pin 3 --------
constexpr uint8_t IOEXP_USB_ADDR = 0x44;
constexpr uint8_t IOEXP_USB_PIN  = 3;

// ---- micro SD (SDMMC slot 0, 4-bit, powered by on-chip LDO channel 4) ---------
constexpr int SD_CLK = 43, SD_CMD = 44, SD_D0 = 39, SD_D1 = 40, SD_D2 = 41, SD_D3 = 42;
constexpr int SD_LDO_CHANNEL = 4;
constexpr const char* SD_MOUNT_POINT = "/sdcard";
constexpr const char* SD_DICT_DIR    = "/sdcard/dict";

// ---- Built-in dictionary partition ------------------------------------------
constexpr const char* DICT_PARTITION_LABEL = "dict";

}  // namespace cfg
