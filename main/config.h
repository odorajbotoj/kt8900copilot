#pragma once

#include <unistd.h>

#include "esp_check.h"

#include "sdcard.h"

#include "esp_mac.h"
#include "mbedtls/md5.h"

typedef struct
{
    char wifi_ssid[32 + 2];
    char wifi_password[16 + 2];
    char ws_server[128];
    char ws_key[128];
    char timezone[60];
    char ntp_server[128];
    int adc_offset;
    int tx_limit_ms;
    bool enable_cam;
} app_config_t;

/*
A good example of conf.txt:

wifi_ssid test
wifi_password examplepass
ws_server wss://example.net:1234/test
ws_key examplekey
timezone CST-8
ntp_server ntp.aliyun.com
adc_offset -2300
tx_limit_ms 60000
enable_cam 0;
*/

extern app_config_t app_config;
extern char device_mac_address[24];
extern uint8_t random_verify[16];
extern uint8_t app_passkey[16];

void get_mac(void);
esp_err_t write_config(void);
void calculate_passkey(void);
void parse_conf_line(const char *input, size_t len);
esp_err_t load_config(void);
void print_config(void);
