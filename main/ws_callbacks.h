#pragma once

#include "esp_check.h"

#include "esp_websocket_client.h"
#include "freertos/FreeRTOS.h"

#include "led.h"
#include "gpio.h"
#include "audio_pwm.h"
#include "afsk1200.h"

typedef enum
{
    WS_STAT_IDLE,
    WS_STAT_RX,
    WS_STAT_TX,
    WS_STAT_IMG,
    WS_STAT_CFG,
} ws_state_t;
extern volatile uint8_t ws_state;
#define SET_STATE(s, v) (ws_state = (ws_state & ~(1 << (s))) | ((v) << (s)))
#define GET_STATE(s) ((ws_state >> (s)) & 1)

extern EventGroupHandle_t ws_event_group;
#define WS_EVT_REFUSE_BIT BIT0
#define WS_EVT_CALL_IMG_BIT BIT1

extern TickType_t last_ptt_on;

extern QueueHandle_t ws_task_play_queue_handle;
extern QueueHandle_t ws_task_afsk_queue_handle;

extern volatile bool verified_client;

void ptt_on(void);
void ptt_off(void);

esp_err_t edit_conf(const char *d, size_t len);

void get_and_upload_img_task(void *arg);
void play_pcm_task(void *arg);
void afsk_send_task(void *arg);

void ws_conn_cb(void *ev_arg, esp_event_base_t ev_base, int32_t ev_id, void *ev_data);
void ws_disconn_cb(void *ev_arg, esp_event_base_t ev_base, int32_t ev_id, void *ev_data);
