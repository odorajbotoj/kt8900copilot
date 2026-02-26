#pragma once

#include "esp_check.h"

#include "pwm_audio.h"

#define PWM_BUF_SIZE 4096

esp_err_t pwm_init(void);
