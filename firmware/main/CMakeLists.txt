idf_component_register(
    SRCS
        "main.cpp"
        "dictionary.cpp"
        "dict_store.cpp"
        "sdcard.cpp"
        "keyboard.cpp"
        "keyboard_i2c.cpp"
        "keyboard_usb.cpp"
        "ui.cpp"
        "settings.cpp"
        "debug_console.cpp"
        "wordbook.cpp"
        "file_replace.cpp"
        "font_store.cpp"
    INCLUDE_DIRS "."
    EMBED_TXTFILES "../../third_party/ejdict/frequency/2000.txt"
    REQUIRES nvs_flash mbedtls vfs esp_driver_usb_serial_jtag usb fatfs sdmmc esp_partition esp_driver_gpio esp_driver_sdmmc
)
