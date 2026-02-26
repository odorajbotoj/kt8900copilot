#pragma once

#include "esp_check.h"

#include "led_indicator.h"
#include "led_indicator_strips.h"

enum
{
    BLINK_RED = 0,
    BLINK_GREEN,
    BLINK_WHITE,
    BLINK_DISCONN,
    BLINK_ERROR,
    BLINK_YELLOW_3,
    BLINK_OFF,
    BLINK_MAX,
};

extern led_indicator_handle_t led_handle;

esp_err_t led_init(void);
