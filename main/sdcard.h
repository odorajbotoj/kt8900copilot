#pragma once

#include "macros.h"

#include "esp_vfs_fat.h"
#include "driver/sdmmc_host.h"

#define MOUNT_POINT "/sdcard"

extern sdmmc_card_t *card;

esp_err_t sdcard_init(void);
