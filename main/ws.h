#pragma once

#include "esp_check.h"

#include "config.h"
#include "bin_data_type.h"
#include "data_packet.h"

#include "esp_websocket_client.h"

#include "led.h"
#include "gpio.h"
#include "audio_pwm.h"

#include "esp_camera.h"
#include "esp_crt_bundle.h"

#include "task_handles.h"

#define WS_BUF_SIZE 8192

extern esp_websocket_client_handle_t ws_client;

typedef enum
{
    WS_STAT_IDLE,
    WS_STAT_RX,
    WS_STAT_TX,
    WS_STAT_IMG,
    WS_STAT_CFG,
} ws_state_t;
extern volatile ws_state_t ws_state;

extern QueueHandle_t ws_send_queue_handle;

esp_err_t websocket_init();
void rig_tx_watchdog(void *arg);
void ws_send_task(void *arg);
