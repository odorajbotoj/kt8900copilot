#pragma once

#include "esp_err.h"
#include "esp_log.h"

#define ERR_CHK(func, format, ...)                \
    do                                            \
    {                                             \
        esp_err_t e = (func);                     \
        if (e != ESP_OK)                          \
        {                                         \
            ESP_LOGE(TAG, format, ##__VA_ARGS__); \
            return e;                             \
        }                                         \
    } while (0)
