#pragma once

#include "macros.h"

#include "driver/gpio.h"

#define GPIO_PTT GPIO_NUM_41
#define GPIO_CTRL GPIO_NUM_42

#define RIG_CTRL_ON 0
#define RIG_CTRL_OFF 1
#define RIG_PTT_ON 0
#define RIG_PTT_OFF 1

esp_err_t gpio_init(void);
