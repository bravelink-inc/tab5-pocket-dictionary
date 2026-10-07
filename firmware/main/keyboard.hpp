#pragma once
#include <cstdint>

// Unified key events from the Tab5 Keyboard (I2C) and USB HID keyboards.
// keycode/modifier follow the USB HID keyboard usage page (0x07).

enum class KeySource : uint8_t { I2C = 0, USB = 1 };

struct KeyEvent {
    KeySource source;
    bool      pressed;   // false = release (USB only; the I2C keyboard reports presses)
    uint8_t   modifier;  // HID modifier bitmask (bit0 LCtrl, bit1 LShift, bit2 LAlt, bit4 RCtrl, bit5 RShift, bit6 RAlt)
    uint8_t   keycode;   // HID usage id
};

namespace hid {
constexpr uint8_t KEY_ENTER = 0x28, KEY_ESC = 0x29, KEY_BACKSPACE = 0x2A, KEY_TAB = 0x2B, KEY_SPACE = 0x2C;
constexpr uint8_t KEY_F1 = 0x3A, KEY_F12 = 0x45;
constexpr uint8_t KEY_HOME = 0x4A, KEY_PAGEUP = 0x4B, KEY_DELETE = 0x4C, KEY_END = 0x4D, KEY_PAGEDOWN = 0x4E;
constexpr uint8_t KEY_RIGHT = 0x4F, KEY_LEFT = 0x50, KEY_DOWN = 0x51, KEY_UP = 0x52;
constexpr uint8_t KEY_KP_ENTER = 0x58;
constexpr uint8_t MOD_CTRL = 0x11, MOD_SHIFT = 0x22, MOD_ALT = 0x44;
}

namespace keyboard {
void init();                                     // create the event queue
bool post(const KeyEvent& ev);                   // callable from any task
bool wait(KeyEvent& ev, uint32_t timeout_ms);    // UI side
char toChar(uint8_t keycode, uint8_t modifier);  // printable ASCII or 0 (US layout)

void startI2C();   // Tab5 Keyboard driver task
void startUSB();   // USB host + HID class driver
bool i2cConnected();
bool usbConnected();
}
