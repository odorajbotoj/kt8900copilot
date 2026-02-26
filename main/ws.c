#include "ws.h"

#define TAG "WS"

esp_websocket_client_handle_t ws_client;

volatile bool g_is_rig_tx = false;

static void ws_data_cb(void *ev_arg, esp_event_base_t ev_base, int32_t ev_id, void *ev_data)
{
    esp_websocket_event_data_t *data = (esp_websocket_event_data_t *)ev_data;
    switch (data->op_code)
    {
    case 0x1:                                                // text
        if (1 == data->data_len && 'T' == *(data->data_ptr)) // 发射
        {
            g_is_rig_tx = true;
            led_indicator_start(led_handle, BLINK_RED);
            gpio_set_level(GPIO_PTT, RIG_PTT_ON);
        }
        if (1 == data->data_len && 'O' == *(data->data_ptr)) // 发射结束
        {
            g_is_rig_tx = false;
            led_indicator_stop(led_handle, BLINK_RED);
            gpio_set_level(GPIO_PTT, RIG_PTT_OFF);
        }
        break;
    case 0x2: // binary
        if (g_is_rig_tx)
        {
            size_t written_bytes = 0;
            pwm_audio_write(data->data_ptr, data->data_len, &written_bytes, 1000 / portTICK_PERIOD_MS);
        }
        break;
    }
}

esp_err_t websocket_init(const char *uri, const char *cert_pem)
{
    esp_websocket_client_config_t ws_cfg = {
        .uri = uri,
        // .cert_pem = cert_pem,
        .buffer_size = 4 * WS_BUF_SIZE,
        .reconnect_timeout_ms = 10000,
        .network_timeout_ms = 10000,
        .enable_close_reconnect = true,
        .disable_auto_reconnect = false,
    };
    ws_client = esp_websocket_client_init(&ws_cfg);
    ERR_CHK(esp_websocket_register_events(ws_client, WEBSOCKET_EVENT_DATA, ws_data_cb, NULL),
            "Failed to register WebSocket event callback.");
    return ESP_OK;
}

void ws_adc_tx_task(void *arg)
{
    // start client
    ESP_ERROR_CHECK(esp_websocket_client_start(ws_client));
    // wait for connection
    while (!esp_websocket_client_is_connected(ws_client))
    {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    adc_continuous_data_t adc_read_buf[ADC_BUF_SIZE / 4] = {0}; // 样本缓冲区
    int16_t ws_send_buf[WS_BUF_SIZE] = {0};                     // 待发数据缓冲区
    uint32_t read_samples = 0;                                  // 读取到的样本数
    esp_err_t err;
    bool ctrl_level;
    uint8_t ctrl_off_delay = 0;
    ESP_LOGI(TAG, "ws_adc_tx_task runs into mainloop.");
    while (1)
    {
        while (g_is_rig_tx || !esp_websocket_client_is_connected(ws_client) || gpio_get_level(GPIO_CTRL) == RIG_CTRL_OFF)
        {
            vTaskDelay(pdMS_TO_TICKS(20)); // 自旋等待
        }
        if (-1 == esp_websocket_client_send_text(ws_client, "R", 1, pdMS_TO_TICKS(50)))
        {
            ESP_LOGE(TAG, "Failed to send start code to server.");
            continue;
        }
        led_indicator_start(led_handle, BLINK_GREEN);
        ctrl_level = gpio_get_level(GPIO_CTRL) == RIG_CTRL_ON;
        while (ctrl_level || ctrl_off_delay)
        {
            if (ctrl_level)
            {
                ctrl_off_delay = 16;
            }
            read_samples = 0;                                                                                                                        // 重置读取计数
            err = adc_continuous_read_parse(adc_handle, adc_read_buf, sizeof(adc_read_buf) / sizeof(adc_read_buf[0]), &read_samples, portMAX_DELAY); // 读取并解析
            if (ESP_OK == err)
            {
                memset(ws_send_buf, 0, sizeof(ws_send_buf)); // 重置待发缓冲区
                for (uint32_t i = 0; i < read_samples; ++i)
                {
                    if (adc_read_buf[i].valid)
                    {
                        ws_send_buf[i] = ((int16_t)(adc_read_buf[i].raw_data) - 2300) << 4; // 处理样本, 2300 is the magic number
                    }
                }
                if (g_is_rig_tx || !esp_websocket_client_is_connected(ws_client))
                {
                    vTaskDelay(pdMS_TO_TICKS(100));
                    break;
                }
                if (-1 == esp_websocket_client_send_bin(ws_client, (const char *)ws_send_buf, read_samples * 2, portMAX_DELAY))
                {
                    vTaskDelay(pdMS_TO_TICKS(100));
                    ESP_LOGE(TAG, "Failed to send pcm data to server.");
                    break;
                }
            }
            --ctrl_off_delay;
            ctrl_level = gpio_get_level(GPIO_CTRL) == RIG_CTRL_ON;
        }
        led_indicator_stop(led_handle, BLINK_GREEN);
        if (-1 == esp_websocket_client_send_text(ws_client, "O", 1, pdMS_TO_TICKS(50)))
        {
            ESP_LOGE(TAG, "Failed to send end code to server.");
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}
