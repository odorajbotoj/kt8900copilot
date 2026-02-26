#pragma once

#include <unistd.h>

#include "esp_check.h"

#include "utils.h"

#include "sdcard.h"

#include "esp_mac.h"

typedef struct
{
    char wifi_ssid[32 + 2];
    char wifi_password[16 + 2];
    char ws_server[128];
    char timezone[60];
    char ntp_server[128];
    int adc_offset;
    int tx_limit_ms;
} app_config_t;

/*
A good example of conf.txt:

wifi_ssid test
wifi_password examplepass
ws_server wss://example.net:1234/test
timezone CST-8
ntp_server ntp.ntsc.ac.cn
adc_offset -2300
tx_limit_ms 120000
*/

extern app_config_t app_config;
extern char device_mac_address[24];

void get_mac(void);
esp_err_t load_config(void);
void print_config(void);
