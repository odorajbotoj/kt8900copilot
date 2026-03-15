#pragma once

#include "esp_check.h"

#include "esp_websocket_client.h"
#include "esp_camera.h"
#include "esp_crt_bundle.h"

#include "config.h"
#include "data_packet.h"
#include "ws_callbacks.h"

#define WS_BUF_SIZE 8192

extern esp_websocket_client_handle_t ws_client;

extern QueueHandle_t ws_send_queue_handle;

extern TaskHandle_t ws_send_task_handle;
extern TaskHandle_t adc_read_task_handle;
extern TaskHandle_t pwm_write_task_handle;
extern TaskHandle_t get_and_upload_img_task_handle;
extern TaskHandle_t play_pcm_task_handle;
extern TaskHandle_t afsk_send_task_handle;

esp_err_t websocket_init();
void rig_tx_watchdog(void *arg);
void ws_send_task(void *arg);
