#include "keyboard.hpp"

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

static QueueHandle_t s_queue = nullptr;

void keyboard::init()
{
    if (!s_queue) s_queue = xQueueCreate(64, sizeof(KeyEvent));
}

bool keyboard::post(const KeyEvent& ev)
{
    return s_queue && xQueueSend(s_queue, &ev, 0) == pdTRUE;
}

bool keyboard::wait(KeyEvent& ev, uint32_t timeout_ms)
{
    return s_queue && xQueueReceive(s_queue, &ev, pdMS_TO_TICKS(timeout_ms)) == pdTRUE;
}

// US ANSI layout: {unshifted, shifted} for usage ids 0x04..0x38
static const char kMap[0x39 - 0x04][2] = {
    {'a','A'},{'b','B'},{'c','C'},{'d','D'},{'e','E'},{'f','F'},{'g','G'},{'h','H'},{'i','I'},{'j','J'},
    {'k','K'},{'l','L'},{'m','M'},{'n','N'},{'o','O'},{'p','P'},{'q','Q'},{'r','R'},{'s','S'},{'t','T'},
    {'u','U'},{'v','V'},{'w','W'},{'x','X'},{'y','Y'},{'z','Z'},
    {'1','!'},{'2','@'},{'3','#'},{'4','$'},{'5','%'},{'6','^'},{'7','&'},{'8','*'},{'9','('},{'0',')'},
    {'\n','\n'},{0,0},{'\b','\b'},{'\t','\t'},{' ',' '},
    {'-','_'},{'=','+'},{'[','{'},{']','}'},{'\\','|'},{'#','~'},{';',':'},{'\'','"'},{'`','~'},{',','<'},{'.','>'},{'/','?'},
};

// [book:9-to-char]
char keyboard::toChar(uint8_t keycode, uint8_t modifier)
{
    const bool shift = (modifier & hid::MOD_SHIFT) != 0;
    if (modifier & (hid::MOD_CTRL | hid::MOD_ALT)) return 0;
    if (keycode >= 0x04 && keycode < 0x39) {
        char c = kMap[keycode - 0x04][shift ? 1 : 0];
        return (c == '\n' || c == '\b' || c == '\t') ? 0 : c;
    }
    // keypad
    switch (keycode) {
        case 0x54: return '/';
        case 0x55: return '*';
        case 0x56: return '-';
        case 0x57: return '+';
        case 0x63: return '.';
        default: break;
    }
    if (keycode >= 0x59 && keycode <= 0x61) return static_cast<char>('1' + (keycode - 0x59));
    if (keycode == 0x62) return '0';
    return 0;
}
// [/book:9-to-char]
