#pragma once

#include "esp_check.h"

#include "config.h"

#include "esp_websocket_client.h"

#include "led.h"
#include "gpio.h"
#include "audio_adc.h"
#include "audio_pwm.h"

#include "esp_camera.h"

#define WS_BUF_SIZE 8192

extern esp_websocket_client_handle_t ws_client;

esp_err_t websocket_init(const char *cert_pem);
void ws_adc_tx_task(void *arg);
