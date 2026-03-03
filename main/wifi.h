#pragma once

#include "esp_check.h"

#include "config.h"

#include "led.h"

#include "freertos/event_groups.h"
#include "nvs_flash.h"
#include "esp_event.h"
#include "esp_wifi.h"

esp_err_t nvs_init(void);
esp_err_t wifi_init(void);
