#pragma once

#include <stdint.h>
#include <stddef.h>
#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"

typedef struct
{
    uint8_t *data;
    size_t len;
    uint8_t code;
} data_packet_t;

void send_to_queue(QueueHandle_t handle, const void *data, size_t len, uint8_t code);
