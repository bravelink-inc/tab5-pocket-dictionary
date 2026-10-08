// USB HID keyboard support on the Tab5's USB-A host port (ESP32-P4 USB 2.0 OTG HS).
// Based on ESP-IDF examples/peripherals/usb/host/hid.

#include "keyboard.hpp"

#include <atomic>
#include <cstring>
#include <M5Unified.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "usb/usb_host.h"
#include "usb/hid_host.h"
#include "usb/hid_usage_keyboard.h"
#include "app_config.hpp"

static const char* TAG = "kbd_usb";
static std::atomic<int> s_keyboards{0};
static QueueHandle_t s_dev_queue = nullptr;

namespace {

struct DevEvent {
    hid_host_device_handle_t handle;
    hid_host_driver_event_t event;
};

// [book:9-usb-power]
// Enable the 5V supply of the USB-A port: PI4IOE5V6408 (0x44) pin 3 as push-pull output high.
void usbPowerOn()
{
    auto& i2c = M5.In_I2C;
    const uint8_t a = cfg::IOEXP_USB_ADDR, bit = 1u << cfg::IOEXP_USB_PIN;
    uint8_t dir = i2c.readRegister8(a, 0x03, 400000);
    i2c.writeRegister8(a, 0x03, dir | bit, 400000);          // direction: output
    uint8_t hiz = i2c.readRegister8(a, 0x07, 400000);
    i2c.writeRegister8(a, 0x07, hiz & ~bit, 400000);         // not high-impedance
    uint8_t out = i2c.readRegister8(a, 0x05, 400000);
    i2c.writeRegister8(a, 0x05, out | bit, 400000);          // high
    ESP_LOGI(TAG, "USB-A 5V enabled (ioexp 0x%02X: dir=0x%02X out=0x%02X)", a, dir | bit, out | bit);
}
// [/book:9-usb-power]

// [book:9-usb-report]
void keyboardReport(const uint8_t* data, size_t length)
{
    if (length < sizeof(hid_keyboard_input_report_boot_t)) return;
    auto* rep = reinterpret_cast<const hid_keyboard_input_report_boot_t*>(data);
    static uint8_t prev[HID_KEYBOARD_KEY_MAX] = {0};

    auto found = [](const uint8_t* keys, uint8_t k) {
        for (int i = 0; i < HID_KEYBOARD_KEY_MAX; ++i) if (keys[i] == k) return true;
        return false;
    };
    for (int i = 0; i < HID_KEYBOARD_KEY_MAX; ++i) {
        if (prev[i] > HID_KEY_ERROR_UNDEFINED && !found(rep->key, prev[i]))
            keyboard::post(KeyEvent{KeySource::USB, false, 0, prev[i]});
        if (rep->key[i] > HID_KEY_ERROR_UNDEFINED && !found(prev, rep->key[i]))
            keyboard::post(KeyEvent{KeySource::USB, true, rep->modifier.val, rep->key[i]});
    }
    memcpy(prev, rep->key, HID_KEYBOARD_KEY_MAX);
}
// [/book:9-usb-report]

void interfaceCallback(hid_host_device_handle_t handle, const hid_host_interface_event_t event, void*)
{
    hid_host_dev_params_t params;
    if (hid_host_device_get_params(handle, &params) != ESP_OK) return;

    switch (event) {
    case HID_HOST_INTERFACE_EVENT_INPUT_REPORT: {
        uint8_t data[64];
        size_t len = 0;
        if (hid_host_device_get_raw_input_report_data(handle, data, sizeof(data), &len) != ESP_OK) return;
        if (params.sub_class == HID_SUBCLASS_BOOT_INTERFACE && params.proto == HID_PROTOCOL_KEYBOARD)
            keyboardReport(data, len);
        break;
    }
    case HID_HOST_INTERFACE_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "HID device disconnected (proto %u)", params.proto);
        if (params.proto == HID_PROTOCOL_KEYBOARD) s_keyboards--;
        hid_host_device_close(handle);
        break;
    case HID_HOST_INTERFACE_EVENT_TRANSFER_ERROR:
        ESP_LOGW(TAG, "HID transfer error");
        break;
    default:
        break;
    }
}

void driverCallback(hid_host_device_handle_t handle, const hid_host_driver_event_t event, void*)
{
    DevEvent ev{handle, event};
    if (s_dev_queue) xQueueSend(s_dev_queue, &ev, 0);
}

void usbLibTask(void* arg)
{
    const usb_host_config_t host_config = {
        .skip_phy_setup = false,
        .root_port_unpowered = false,
        .intr_flags = ESP_INTR_FLAG_LEVEL1,
        .enum_filter_cb = nullptr,
        .fifo_settings_custom = {},
        .peripheral_map = 0,     // default = the high-speed peripheral (USB-A port) on ESP32-P4
    };
    esp_err_t err = usb_host_install(&host_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "usb_host_install failed: %s", esp_err_to_name(err));
        vTaskDelete(nullptr);
        return;
    }
    xTaskNotifyGive(static_cast<TaskHandle_t>(arg));
    for (;;) {
        uint32_t flags = 0;
        usb_host_lib_handle_events(portMAX_DELAY, &flags);
        if (flags & USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS) usb_host_device_free_all();
    }
}

// [book:9-usb-connect]
// [book:9-usb-connect-start]
void hidTask(void*)
{
    TaskHandle_t self = xTaskGetCurrentTaskHandle();
    xTaskCreatePinnedToCore(usbLibTask, "usb_lib", 4096, self, 10, nullptr, 0);
    if (ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(3000)) == 0) {
        ESP_LOGE(TAG, "USB host did not start");
        vTaskDelete(nullptr);
        return;
    }

    const hid_host_driver_config_t drv = {
        .create_background_task = true,
        .task_priority = 5,
        .stack_size = 4096,
        .core_id = 0,
        .callback = driverCallback,
        .callback_arg = nullptr,
    };
// [/book:9-usb-connect-start]
    esp_err_t err = hid_host_install(&drv);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "hid_host_install failed: %s", esp_err_to_name(err));
        vTaskDelete(nullptr);
        return;
    }
    ESP_LOGI(TAG, "USB host ready, waiting for a keyboard");

    for (;;) {
        DevEvent ev;
        if (!xQueueReceive(s_dev_queue, &ev, portMAX_DELAY)) continue;
        if (ev.event != HID_HOST_DRIVER_EVENT_CONNECTED) continue;

        hid_host_dev_params_t params;
// [book:9-usb-connect-end]
        if (hid_host_device_get_params(ev.handle, &params) != ESP_OK) continue;
        ESP_LOGI(TAG, "HID device connected: subclass %u proto %u", params.sub_class, params.proto);

        const hid_host_device_config_t dev_cfg = { .callback = interfaceCallback, .callback_arg = nullptr };
        if (hid_host_device_open(ev.handle, &dev_cfg) != ESP_OK) continue;
        if (params.sub_class == HID_SUBCLASS_BOOT_INTERFACE) {
            hid_class_request_set_protocol(ev.handle, HID_REPORT_PROTOCOL_BOOT);
            if (params.proto == HID_PROTOCOL_KEYBOARD) hid_class_request_set_idle(ev.handle, 0, 0);
        }
        if (hid_host_device_start(ev.handle) == ESP_OK && params.proto == HID_PROTOCOL_KEYBOARD) s_keyboards++;
    }
}
// [/book:9-usb-connect-end]
// [/book:9-usb-connect]

}  // namespace

bool keyboard::usbConnected() { return s_keyboards > 0; }

void keyboard::startUSB()
{
    usbPowerOn();
    if (!s_dev_queue) s_dev_queue = xQueueCreate(8, sizeof(DevEvent));
    xTaskCreatePinnedToCore(hidTask, "usb_hid", 4096, nullptr, 5, nullptr, 0);
}
