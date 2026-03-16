
#include "data_packet.h"

#define TAG "DATA_PACKET"

void send_to_queue(QueueHandle_t handle, const void *data, size_t len)
{
    data_packet_t pkt;
    pkt.data = heap_caps_malloc(len, MALLOC_CAP_SPIRAM);
    if (!pkt.data)
    {
        ESP_LOGE(TAG, "heap_caps_malloc failed. length: %zu", len);
        return;
    }
    memcpy(pkt.data, data, len);
    pkt.len = len;
    if (xQueueSend(handle, &pkt, 2) != pdTRUE)
    {
        ESP_LOGE(TAG, "queue full, dropping packet (len=%zu)", len);
        free(pkt.data);
    }
}

void send_to_ws(const void *data, size_t len, uint8_t code)
{
    ws_data_packet_t pkt;
    if (data == NULL)
    {
        pkt.data = NULL;
        pkt.len = 0;
    }
    else if (code == 0xFF)
    {
        pkt.data = heap_caps_malloc(len, MALLOC_CAP_SPIRAM);
        if (!pkt.data)
        {
            ESP_LOGE(TAG, "heap_caps_malloc failed. length: %zu", len);
            return;
        }
        memcpy(pkt.data, data, len);
        pkt.len = len;
    }
    else
    {
        pkt.data = heap_caps_malloc(len + 3, MALLOC_CAP_SPIRAM);
        if (!pkt.data)
        {
            ESP_LOGE(TAG, "heap_caps_malloc failed. length: %zu", len);
            return;
        }
        pkt.data[0] = code;
        pkt.data[1] = len & 0xFF;
        pkt.data[2] = (len >> 8) & 0xFF;
        memcpy(pkt.data + 3, data, len);
        pkt.len = len + 3;
    }
    pkt.code = code;
    if (xQueueSend(ws_send_queue_handle, &pkt, 2) != pdTRUE)
    {
        ESP_LOGE(TAG, "queue full, dropping packet (len=%zu)", len);
        free(pkt.data);
    }
}
