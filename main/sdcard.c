#include "sdcard.h"

#define TAG "SDCARD"

sdmmc_card_t *card;

esp_err_t sdcard_init(void)
{
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    sdmmc_slot_config_t slot_conf = SDMMC_SLOT_CONFIG_DEFAULT();
    slot_conf.width = 1;
    slot_conf.d0 = GPIO_NUM_40;
    slot_conf.clk = GPIO_NUM_39;
    slot_conf.cmd = GPIO_NUM_38;
    esp_vfs_fat_sdmmc_mount_config_t mount_conf = {
        .format_if_mount_failed = false,
        .max_files = 4,
    };
    ESP_RETURN_ON_ERROR(esp_vfs_fat_sdmmc_mount(MOUNT_POINT, &host, &slot_conf, &mount_conf, &card), TAG, "Failed to mount filesystem.");
    return ESP_OK;
}
