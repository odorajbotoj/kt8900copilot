#include "ws.h"

#define TAG "WS"

esp_websocket_client_handle_t ws_client;

TaskHandle_t ws_send_task_handle;
TaskHandle_t rig_tx_watchdog_handle;
TaskHandle_t get_and_upload_img_task_handle;
TaskHandle_t play_pcm_task_handle;
TaskHandle_t afsk_send_task_handle;

// data processing callback
static void ws_data_cb(void *ev_arg, esp_event_base_t ev_base, int32_t ev_id, void *ev_data)
{
    esp_websocket_event_data_t *data = (esp_websocket_event_data_t *)ev_data;
    if (ws_state <= WS_STAT_TX && data->op_code == 0x02 && data->data_ptr)
    {
        switch (*(data->data_ptr))
        {
        case CTRL_CODE_PCM:
            if (ws_state == WS_STAT_TX)
            {
                send_to_queue(pwm_write_queue_handle, data->data_ptr + 1, (data->data_len) - 1, 0);
            }
            return;
        case CTRL_CODE_TX:
            ptt_on();
            return;
        case CTRL_CODE_TX_STOP:
            ptt_off();
            return;
        case CTRL_CODE_PLAY:
            send_to_queue(ws_task_play_queue_handle, data->data_ptr + 1, data->data_len - 1, 0);
            return;
        case CTRL_CODE_AFSK:
            ESP_RETURN_VOID_ON_FALSE(data->data_len > 2, TAG, "invalid AFSK packet.");
            send_to_queue(ws_task_afsk_queue_handle, data->data_ptr + 1, data->data_len - 1, 0);
            return;
        case CTRL_CODE_IMG_GET:
            xEventGroupSetBits(ws_event_group, WS_EVT_CALL_IMG_BIT);
            return;
        case CTRL_CODE_SET_CONF:
            ESP_RETURN_VOID_ON_ERROR(edit_conf(data->data_ptr + 1, data->data_len - 1), TAG, "edit_conf failed.");
            return;
        case CTRL_CODE_RESET:
            ESP_LOGW(TAG, "get restart.");
            esp_restart();
            return;
        case CTRL_CODE_VERIFY:
            if (data->data_len != 17)
                return;
            memcpy(random_verify, data->data_ptr + 1, 16);
            calculate_passkey();
            send_to_queue(ws_send_queue_handle, app_passkey, 16, CTRL_CODE_PASSTHROUGH);
            return;
        case CTRL_CODE_VERIFIED:
            verified_client = true;
            ESP_LOGI(TAG, "client verified.");
            return;
        case CTRL_CODE_REFUSE:
            ESP_LOGE(TAG, "Server refused the connection.");
            xEventGroupSetBits(ws_event_group, WS_EVT_REFUSE_BIT);
            return;
        case CTRL_CODE_CONN_BUSY:
            ESP_LOGE(TAG, "Server returned: the connection is busy. please wait a minute");
            return;
        }
    }
}

void ws_destroy_task(void *arg)
{
    EventBits_t bits;
    for (;;)
    {
        bits = xEventGroupWaitBits(ws_event_group, WS_EVT_REFUSE_BIT, pdTRUE, pdFALSE, portMAX_DELAY);
        if (bits & WS_EVT_REFUSE_BIT)
        {
            esp_websocket_client_stop(ws_client);
            esp_websocket_client_destroy(ws_client);
            ESP_LOGW(TAG, "connection refused, websocket client closed.");
            if (ws_state > WS_STAT_RX)
                ptt_off();
            if (adc_read_task_handle && eTaskGetState(adc_read_task_handle) < eDeleted)
                vTaskDelete(adc_read_task_handle);
            if (get_and_upload_img_task_handle && eTaskGetState(get_and_upload_img_task_handle) < eDeleted)
                vTaskDelete(get_and_upload_img_task_handle);
            if (play_pcm_task_handle && eTaskGetState(play_pcm_task_handle) < eDeleted)
                vTaskDelete(play_pcm_task_handle);
            if (afsk_send_task_handle && eTaskGetState(afsk_send_task_handle) < eDeleted)
                vTaskDelete(afsk_send_task_handle);
            if (ws_send_task_handle && eTaskGetState(ws_send_task_handle) < eDeleted)
                vTaskDelete(ws_send_task_handle);
            if (pwm_write_task_handle && eTaskGetState(pwm_write_task_handle) < eDeleted)
                vTaskDelete(pwm_write_task_handle);
            if (rig_tx_watchdog_handle && eTaskGetState(rig_tx_watchdog_handle) < eDeleted)
                vTaskDelete(rig_tx_watchdog_handle);
            vTaskDelete(NULL);
        }
    }
}

// 初始化ws客户端
esp_err_t websocket_init()
{
    esp_websocket_client_config_t ws_cfg = {
        .uri = app_config.ws_server,
        .buffer_size = WS_BUF_SIZE,
        .reconnect_timeout_ms = 5000,
        .network_timeout_ms = 5000,
        .enable_close_reconnect = true,
        .disable_auto_reconnect = false,
        .task_stack = 2 * WS_BUF_SIZE,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .ping_interval_sec = 30,
        .disable_pingpong_discon = false,
        .pingpong_timeout_sec = 10,
    };
    ws_client = esp_websocket_client_init(&ws_cfg);
    ESP_RETURN_ON_ERROR(esp_websocket_register_events(ws_client, WEBSOCKET_EVENT_DATA, ws_data_cb, NULL),
                        TAG,
                        "Failed to register WebSocket data event callback.");
    ESP_RETURN_ON_ERROR(esp_websocket_register_events(ws_client, WEBSOCKET_EVENT_CONNECTED, ws_conn_cb, NULL),
                        TAG,
                        "Failed to register WebSocket conn event callback.");
    ESP_RETURN_ON_ERROR(esp_websocket_register_events(ws_client, WEBSOCKET_EVENT_DISCONNECTED, ws_disconn_cb, NULL),
                        TAG,
                        "Failed to register WebSocket disconn event callback.");
    ws_event_group = xEventGroupCreate();
    xTaskCreatePinnedToCoreWithCaps(ws_destroy_task, "ws_destroy_task", 2 * 1024, NULL, 5, NULL, 1, MALLOC_CAP_SPIRAM);

    xTaskCreatePinnedToCoreWithCaps(get_and_upload_img_task,
                                    "get_and_upload_img_task",
                                    512 * 1024,
                                    NULL,
                                    2,
                                    &get_and_upload_img_task_handle,
                                    1,
                                    MALLOC_CAP_SPIRAM);
    ws_task_play_queue_handle = xQueueCreate(16, sizeof(data_packet_t));
    ESP_RETURN_ON_FALSE(ws_task_play_queue_handle, ESP_FAIL, TAG, "failed to create ws_task_play_queue_handle.");
    ws_task_afsk_queue_handle = xQueueCreate(16, sizeof(data_packet_t));
    ESP_RETURN_ON_FALSE(ws_task_afsk_queue_handle, ESP_FAIL, TAG, "failed to create ws_task_afsk_queue_handle.");
    xTaskCreatePinnedToCoreWithCaps(play_pcm_task,
                                    "play_pcm_task",
                                    16 * 1024,
                                    NULL,
                                    2,
                                    &play_pcm_task_handle,
                                    1,
                                    MALLOC_CAP_SPIRAM);
    xTaskCreatePinnedToCoreWithCaps(afsk_send_task,
                                    "afsk_send_task",
                                    16 * 1024,
                                    NULL,
                                    2,
                                    &afsk_send_task_handle,
                                    1,
                                    MALLOC_CAP_SPIRAM);

    verified_client = false;

    return ESP_OK;
}

void rig_tx_watchdog(void *arg)
{
    ESP_LOGI(TAG, "rig_tx_watchdog runs into mainloop.");
    for (;;)
    {
        vTaskDelay(pdMS_TO_TICKS(100));
        if (ws_state == WS_STAT_TX)
        {
            if (app_config.tx_limit_ms > 0 && xTaskGetTickCount() - last_ptt_on > pdMS_TO_TICKS(app_config.tx_limit_ms))
            {
                ptt_off();
                ESP_LOGW(TAG, "rig_tx_watchdog set ptt_off: exceed time limit.");
                continue;
            }
            if (xTaskGetTickCount() - last_pwm_write > pdMS_TO_TICKS(750))
            {
                ptt_off();
                ESP_LOGW(TAG, "rig_tx_watchdog set ptt_off: slow pwm write.");
                continue;
            }
        }
    }
}

void ws_send_task(void *arg)
{
    data_packet_t pkt;
    char code_temp[1];
    ESP_LOGI(TAG, "ws_send_task runs into mainloop.");
    for (;;)
    {
        if (xQueueReceive(ws_send_queue_handle, &pkt, portMAX_DELAY))
        {
            if ((verified_client && esp_websocket_client_is_connected(ws_client)) || pkt.code == CTRL_CODE_PASSTHROUGH)
            {
                if (pkt.code == 0 || pkt.code == CTRL_CODE_PASSTHROUGH)
                {
                    esp_websocket_client_send_bin(ws_client, (const char *)pkt.data, pkt.len, portMAX_DELAY);
                    free(pkt.data);
                }
                else
                {
                    code_temp[0] = pkt.code;
                    esp_websocket_client_send_bin(ws_client, (const char *)code_temp, 1, portMAX_DELAY);
                }
            }
        }
    }
}
