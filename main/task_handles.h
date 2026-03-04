#pragma once

#include "freertos/FreeRTOS.h"

extern TaskHandle_t ws_send_task_handle;
extern TaskHandle_t adc_read_task_handle;
extern TaskHandle_t pwm_write_task_handle;
extern TaskHandle_t get_and_upload_img_task_handle;
