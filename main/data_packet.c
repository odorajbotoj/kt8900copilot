
#include "data_packet.h"

#define TAG "DATA_PACKET"

void send_to_queue(QueueHandle_t handle, const void *data, size_t len, uint8_t code)
{
    data_packet_t pkt;
    if (!code)
    {
        pkt.data = heap_caps_malloc(len, MALLOC_CAP_SPIRAM);
        if (!pkt.data)
        {
            ESP_LOGE(TAG, "heap_caps_malloc failed. length: %zu", len);
            return;
        }
        memcpy(pkt.data, data, len);
        pkt.len = len;
        pkt.code = 0;
    }
    else
    {
        pkt.code = code;
    }
    xQueueSend(handle, &pkt, 0);
}
