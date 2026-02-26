#pragma once

#include "esp_check.h"

#include "esp_adc/adc_continuous.h"

#define ADC_BUF_SIZE 2048

extern adc_continuous_handle_t adc_handle;

esp_err_t adc_init(void);
