#pragma once

#include "esp_check.h"

#include "data_packet.h"
#include "ws.h"

#include "pwm_audio.h"

#define PWM_BUF_SIZE 1024

extern QueueHandle_t pwm_write_queue_handle;

esp_err_t pwm_init(void);
void pwm_write_task(void *arg);
