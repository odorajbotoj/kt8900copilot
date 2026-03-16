#pragma once

#include <stdint.h>
#include <stddef.h>
#include <string.h>

#include "esp_check.h"
#include "freertos/FreeRTOS.h"

// connection control
#define CTRL_CODE_SKIP 0x00
#define CTRL_CODE_VERIFY 0x01
#define CTRL_CODE_REFUSE 0x02
#define CTRL_CODE_CONN_BUSY 0x03
#define CTRL_CODE_VERIFIED 0x04
// device control
#define CTRL_CODE_RX 0x11
#define CTRL_CODE_RX_STOP 0x12
#define CTRL_CODE_TX 0x13
#define CTRL_CODE_TX_STOP 0x14
#define CTRL_CODE_SET_CONF 0x15
#define CTRL_CODE_RESET 0x16
#define CTRL_CODE_PLAY 0x17
#define CTRL_CODE_AFSK 0x18
// operation result
#define CTRL_CODE_S_MESSAGE 0x31
#define CTRL_CODE_S_S_SET_CONF 0x32
#define CTRL_CODE_S_E_SET_CONF 0x33
#define CTRL_CODE_S_E_CAM_DISABLED 0x34
#define CTRL_CODE_S_S_PLAY 0x35
#define CTRL_CODE_S_E_PLAY 0x36
// client status
#define CTRL_CODE_FROM 0x51
#define CTRL_CODE_ONLINE 0x52
#define CTRL_CODE_OFFLINE 0x53
// binary data
#define CTRL_CODE_PCM 0x71
// special
#define CTRL_CODE_PASSTHROUGH 0xFF

extern QueueHandle_t ws_send_queue_handle;

typedef struct
{
    uint8_t *data;
    size_t len;
} data_packet_t;

typedef struct
{
    uint8_t *data;
    size_t len;
    uint8_t code;
} ws_data_packet_t;

void send_to_queue(QueueHandle_t handle, const void *data, size_t len);
void send_to_ws(const void *data, size_t len, uint8_t code);
