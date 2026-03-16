#pragma once

#include "esp_check.h"

#include "esp_websocket_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "led.h"
#include "gpio.h"
#include "audio_pwm.h"
#include "afsk1200.h"

typedef enum
{
    WS_STAT_TX = 1,
    WS_STAT_RX = 1 << 1,
    WS_STAT_AFSK = 1 << 2,
    WS_STAT_PLAY = 1 << 3,
    WS_STAT_CFG = 1 << 4,
} ws_state_t;
void ws_state_set(ws_state_t state, bool v);
bool ws_state_check(uint8_t v);
bool ws_state_idle(void);

extern EventGroupHandle_t ws_event_group;
#define WS_EVT_REFUSE_BIT BIT0

extern TickType_t last_ptt_on;

extern QueueHandle_t ws_task_play_queue_handle;
extern QueueHandle_t ws_task_afsk_queue_handle;

extern volatile bool verified_client;

void ptt_on(void);
void ptt_off(void);

esp_err_t edit_conf(const char *d, size_t len);

void play_pcm_task(void *arg);
void afsk_send_task(void *arg);

void ws_conn_cb(void *ev_arg, esp_event_base_t ev_base, int32_t ev_id, void *ev_data);
void ws_disconn_cb(void *ev_arg, esp_event_base_t ev_base, int32_t ev_id, void *ev_data);
