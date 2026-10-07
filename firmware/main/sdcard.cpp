#include "sdcard.hpp"

#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "driver/sdmmc_host.h"
#include "sd_pwr_ctrl_by_on_chip_ldo.h"
#include "app_config.hpp"

static const char* TAG = "sdcard";
static sdmmc_card_t* s_card = nullptr;

bool sdcard_mounted() { return s_card != nullptr; }

// [book:11-sd-mount]
bool sdcard_mount()
{
    if (s_card) return true;

    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.slot = SDMMC_HOST_SLOT_0;
    host.max_freq_khz = SDMMC_FREQ_HIGHSPEED;

    // The Tab5 SD slot is powered from the ESP32-P4 on-chip LDO (VO4).
    sd_pwr_ctrl_ldo_config_t ldo_cfg = { .ldo_chan_id = cfg::SD_LDO_CHANNEL };
    sd_pwr_ctrl_handle_t pwr = nullptr;
    esp_err_t err = sd_pwr_ctrl_new_on_chip_ldo(&ldo_cfg, &pwr);
    if (err != ESP_OK) { ESP_LOGE(TAG, "LDO init failed: %s", esp_err_to_name(err)); return false; }
    host.pwr_ctrl_handle = pwr;

    sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
    slot.clk = static_cast<gpio_num_t>(cfg::SD_CLK);
    slot.cmd = static_cast<gpio_num_t>(cfg::SD_CMD);
    slot.d0  = static_cast<gpio_num_t>(cfg::SD_D0);
    slot.d1  = static_cast<gpio_num_t>(cfg::SD_D1);
    slot.d2  = static_cast<gpio_num_t>(cfg::SD_D2);
    slot.d3  = static_cast<gpio_num_t>(cfg::SD_D3);
    slot.cd = SDMMC_SLOT_NO_CD;
    slot.wp = SDMMC_SLOT_NO_WP;
    slot.width = 4;
    slot.flags = 0;

    esp_vfs_fat_sdmmc_mount_config_t mount = {};
    mount.format_if_mount_failed = false;
    mount.max_files = 12;
    mount.allocation_unit_size = 16 * 1024;

    err = esp_vfs_fat_sdmmc_mount(cfg::SD_MOUNT_POINT, &host, &slot, &mount, &s_card);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "mount failed: %s (no card, or not FAT32?)", esp_err_to_name(err));
        s_card = nullptr;
        sd_pwr_ctrl_del_on_chip_ldo(pwr);
        return false;
    }
    ESP_LOGI(TAG, "mounted %s: %s %lluMB", cfg::SD_MOUNT_POINT, s_card->cid.name,
             ((uint64_t)s_card->csd.capacity * s_card->csd.sector_size) >> 20);
    return true;
}
// [/book:11-sd-mount]
