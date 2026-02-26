#include "ws.h"

#define TAG "WS"

esp_websocket_client_handle_t ws_client;

static volatile bool g_is_rig_tx = false; // rig tx
static TickType_t last_ptt_on;            // used for calculating tx time limit

/*
Control Codes:
O: generic stop
E: generic error
T: s->c for tx
R: c->s for uploading rx
I: s->c for getting image, c->s for uploading image
*/

static inline void ptt_on(void)
{
    g_is_rig_tx = true;
    ESP_LOGI(TAG, "PTT_ON");
    led_indicator_start(led_handle, BLINK_RED);
    gpio_set_level(GPIO_PTT, RIG_PTT_ON);
}
static inline void ptt_off(void)
{
    g_is_rig_tx = false;
    ESP_LOGI(TAG, "PTT_OFF");
    led_indicator_stop(led_handle, BLINK_RED);
    gpio_set_level(GPIO_PTT, RIG_PTT_OFF);
}
static inline esp_err_t get_and_upload_img(void)
{
    led_indicator_start(led_handle, BLINK_WHITE);
    // refresh buffer
    esp_camera_fb_return(esp_camera_fb_get());
    // acquire a frame
    camera_fb_t *fb = esp_camera_fb_get();
    if (fb == NULL)
    {
        ESP_RETURN_ON_FALSE(esp_websocket_client_is_connected(ws_client) &&
                                (-1 != esp_websocket_client_send_bin(ws_client,
                                                                     "E\0img nil",
                                                                     9,
                                                                     portMAX_DELAY)),
                            ESP_FAIL,
                            TAG,
                            "Failed to send img error signal to server.");
        ESP_RETURN_ON_FALSE(fb, ESP_FAIL, TAG, "Camera Capture Failed");
    }
    // begin
    ESP_RETURN_ON_FALSE(esp_websocket_client_is_connected(ws_client) &&
                            (-1 != esp_websocket_client_send_text(ws_client,
                                                                  "I",
                                                                  1,
                                                                  portMAX_DELAY)),
                        ESP_FAIL,
                        TAG,
                        "Failed to send img begin signal to server.");
    // sending
    for (size_t i = 0; i < fb->len; i += WS_BUF_SIZE)
    {
        ESP_RETURN_ON_FALSE(esp_websocket_client_is_connected(ws_client) &&
                                (-1 != esp_websocket_client_send_bin(ws_client,
                                                                     (const char *)((fb->buf) + i),
                                                                     fb->len - i >= WS_BUF_SIZE ? WS_BUF_SIZE : fb->len - i,
                                                                     portMAX_DELAY)),
                            ESP_FAIL,
                            TAG,
                            "Failed to send img data to server.");
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    // end
    ESP_RETURN_ON_FALSE(esp_websocket_client_is_connected(ws_client) &&
                            (-1 != esp_websocket_client_send_text(ws_client,
                                                                  "O",
                                                                  1,
                                                                  portMAX_DELAY)),
                        ESP_FAIL,
                        TAG,
                        "Failed to send img end signal to server.");
    // free
    esp_camera_fb_return(fb);
    led_indicator_stop(led_handle, BLINK_WHITE);
    ESP_LOGI(TAG, "IMG_UPLOADED");
    return ESP_OK;
}

static void ws_data_cb(void *ev_arg, esp_event_base_t ev_base, int32_t ev_id, void *ev_data)
{
    esp_websocket_event_data_t *data = (esp_websocket_event_data_t *)ev_data;
    switch (data->op_code)
    {
    case 0x2: // binary
        if (g_is_rig_tx)
        {
            if (app_config.tx_limit_ms > 0 && xTaskGetTickCount() - last_ptt_on > pdMS_TO_TICKS(app_config.tx_limit_ms))
            {
                ptt_off();
                return;
            }

            size_t written_bytes = 0;
            pwm_audio_write((uint8_t *)data->data_ptr, data->data_len, &written_bytes, 1000 / portTICK_PERIOD_MS);
            return;
        }
        break;
    case 0x1: // text

        // ptt
        if (1 == data->data_len && 'T' == *(data->data_ptr)) // 发射
        {
            last_ptt_on = xTaskGetTickCount();
            ptt_on();
            return;
        }
        if (1 == data->data_len && 'O' == *(data->data_ptr)) // 发射结束
        {
            ptt_off();
            return;
        }

        // ask for image
        if (1 == data->data_len && 'I' == *(data->data_ptr))
        {
            ESP_RETURN_VOID_ON_ERROR(get_and_upload_img(), TAG, "get_and_upload_img failed.");
            return;
        }

        break;
    }
}

static void ws_conn_cb(void *ev_arg, esp_event_base_t ev_base, int32_t ev_id, void *ev_data)
{
    led_indicator_stop(led_handle, BLINK_DISCONN);
}
static void ws_disconn_cb(void *ev_arg, esp_event_base_t ev_base, int32_t ev_id, void *ev_data)
{
    led_indicator_start(led_handle, BLINK_DISCONN);
}

esp_err_t websocket_init(const char *cert_pem)
{
    esp_websocket_client_config_t ws_cfg = {
        .uri = app_config.ws_server,
        // .cert_pem = cert_pem,
        .buffer_size = WS_BUF_SIZE,
        .reconnect_timeout_ms = 10000,
        .network_timeout_ms = 10000,
        .enable_close_reconnect = true,
        .disable_auto_reconnect = false,
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
    return ESP_OK;
}

void ws_adc_tx_task(void *arg)
{
    // start client
    ESP_ERROR_CHECK(esp_websocket_client_start(ws_client));
    led_indicator_start(led_handle, BLINK_DISCONN);
    // wait for connection
    while (!esp_websocket_client_is_connected(ws_client))
    {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    led_indicator_stop(led_handle, BLINK_DISCONN);
    adc_continuous_data_t adc_read_buf[ADC_BUF_SIZE] = {0}; // 样本缓冲区
    int16_t ws_send_buf[ADC_BUF_SIZE] = {0};                // 待发数据缓冲区
    uint32_t read_samples = 0;                              // 读取到的样本数
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
        ESP_LOGI(TAG, "VOICE_ON");
        led_indicator_start(led_handle, BLINK_GREEN);
        ctrl_level = gpio_get_level(GPIO_CTRL) == RIG_CTRL_ON;
        while (ctrl_level || ctrl_off_delay)
        {
            if (ctrl_level)
            {
                ctrl_off_delay = 16;
            }
            read_samples = 0;                                                                                                                        // reset count
            err = adc_continuous_read_parse(adc_handle, adc_read_buf, sizeof(adc_read_buf) / sizeof(adc_read_buf[0]), &read_samples, portMAX_DELAY); // read and parse
            if (ESP_OK == err)
            {
                memset(ws_send_buf, 0, sizeof(ws_send_buf)); // reset send buffer
                for (uint32_t i = 0; i < read_samples; ++i)
                {
                    if (adc_read_buf[i].valid)
                    {
                        ws_send_buf[i] = ((int16_t)(adc_read_buf[i].raw_data) + app_config.adc_offset) << 4; // process samples
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
        ESP_LOGI(TAG, "VOICE_OFF");
        if (-1 == esp_websocket_client_send_text(ws_client, "O", 1, pdMS_TO_TICKS(50)))
        {
            ESP_LOGE(TAG, "Failed to send end code to server.");
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}
