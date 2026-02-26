#pragma once

#include "macros.h"
#include "utils.h"

#include "sdcard.h"

typedef struct
{
    char wifi_ssid[32 + 2];
    char wifi_password[16 + 2];
    char server_addr[128];
    int adc_offset;
} app_config_t;

extern app_config_t app_config;

esp_err_t load_config();
