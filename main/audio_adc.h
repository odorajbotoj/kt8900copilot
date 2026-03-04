#pragma once

#include "esp_check.h"

#include "data_packet.h"

#include "esp_adc/adc_continuous.h"

#include "gpio.h"
#include "ws.h"

#define ADC_BUF_SIZE 1024

extern adc_continuous_handle_t adc_handle;

esp_err_t adc_init(void);
void adc_read_task(void *arg);
