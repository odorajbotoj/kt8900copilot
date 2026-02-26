#pragma once

#include "macros.h"

#include "led.h"

#include "freertos/event_groups.h"
#include "nvs_flash.h"
#include "esp_event.h"
#include "esp_wifi.h"

#define WIFI_MAX_RETRY 3

esp_err_t nvs_init(void);
esp_err_t wifi_init(const char *ssid, const char *password);
