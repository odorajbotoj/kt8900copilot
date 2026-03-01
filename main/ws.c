#include "ws.h"

#define TAG "WS"

esp_websocket_client_handle_t ws_client;

static volatile bool g_is_rig_tx = false; // rig tx
static volatile bool g_is_img_on = false; // when taking photos

TaskHandle_t ws_adc_tx_task_handle;
static EventGroupHandle_t ws_event_group;
#define WS_EVT_REFUSE_BIT BIT0

static TickType_t last_ptt_on; // used for calculating tx time limit

static const char CTRL_CODE[] = {0x00, 0x01, 0x02, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x31, 0x32, 0x33, 0x51, 0x61};
enum
{
    SKIP = 0,
    VERIFY,
    REFUSE,
    RX,
    RX_STOP,
    TX,
    TX_STOP,
    IMG_UPLOAD,
    IMG_UPLOAD_STOP,
    IMG_GET,
    SET_CONF,
    RESET,
    S_E_IMG_NIL,
    S_S_SET_CONF,
    S_E_SET_CONF,
    PCM,
    IMG,
};
#define CTRL_CODE_SKIP 0x00
#define CTRL_CODE_VERIFY 0x01
#define CTRL_CODE_REFUSE 0x02
#define CTRL_CODE_RX 0x11
#define CTRL_CODE_RX_STOP 0x12
#define CTRL_CODE_TX 0x13
#define CTRL_CODE_TX_STOP 0x14
#define CTRL_CODE_IMG_UPLOAD 0x15
#define CTRL_CODE_IMG_UPLOAD_STOP 0x16
#define CTRL_CODE_IMG_GET 0x17
#define CTRL_CODE_SET_CONF 0x18
#define CTRL_CODE_RESET 0x19
#define CTRL_CODE_S_E_IMG_NIL 0x31
#define CTRL_CODE_S_S_SET_CONF 0x32
#define CTRL_CODE_S_E_SET_CONF 0x33
#define CTRL_CODE_PCM 0x51
#define CTRL_CODE_IMG 0x61

// functions below are for data processing callback
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
    g_is_img_on = true;
    esp_err_t ret = ESP_OK;
    led_indicator_start(led_handle, BLINK_WHITE);
    // refresh buffer
    esp_camera_fb_return(esp_camera_fb_get());
    // acquire a frame
    camera_fb_t *fb = esp_camera_fb_get();
    if (fb == NULL)
    {
        ESP_GOTO_ON_FALSE(esp_websocket_client_is_connected(ws_client) &&
                              (-1 != esp_websocket_client_send_bin(ws_client,
                                                                   CTRL_CODE + S_E_IMG_NIL,
                                                                   1,
                                                                   pdMS_TO_TICKS(50))),
                          ESP_FAIL,
                          err,
                          TAG,
                          "Failed to send img error signal to server.");
        ESP_GOTO_ON_FALSE(fb, ESP_FAIL, err, TAG, "Camera Capture Failed");
    }
    // begin
    ESP_GOTO_ON_FALSE(esp_websocket_client_is_connected(ws_client) &&
                          (-1 != esp_websocket_client_send_bin(ws_client,
                                                               CTRL_CODE + IMG_UPLOAD,
                                                               1,
                                                               pdMS_TO_TICKS(50))),
                      ESP_FAIL,
                      err,
                      TAG,
                      "Failed to send img begin signal to server.");
    // sending
    char buf[WS_BUF_SIZE] = {CTRL_CODE[IMG], 0};
    for (size_t i = 0; i < fb->len; i += WS_BUF_SIZE - 1)
    {
        size_t valid_len = fb->len - i >= WS_BUF_SIZE - 1 ? WS_BUF_SIZE - 1 : fb->len - i;
        memcpy(buf + 1, (fb->buf) + i, valid_len);
        ESP_GOTO_ON_FALSE(esp_websocket_client_is_connected(ws_client) &&
                              (-1 != esp_websocket_client_send_bin(ws_client,
                                                                   buf,
                                                                   valid_len + 1,
                                                                   pdMS_TO_TICKS(100))),
                          ESP_FAIL,
                          err,
                          TAG,
                          "Failed to send img data to server.");
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    // end
    ESP_GOTO_ON_FALSE(esp_websocket_client_is_connected(ws_client) &&
                          (-1 != esp_websocket_client_send_bin(ws_client,
                                                               CTRL_CODE + IMG_UPLOAD_STOP,
                                                               1,
                                                               pdMS_TO_TICKS(50))),
                      ESP_FAIL,
                      err,
                      TAG,
                      "Failed to send img end signal to server.");
    // free
    esp_camera_fb_return(fb);
    led_indicator_stop(led_handle, BLINK_WHITE);
    ESP_LOGI(TAG, "IMG_UPLOADED");
    g_is_img_on = false;
    return ret;
err:
    esp_camera_fb_return(fb);
    led_indicator_stop(led_handle, BLINK_WHITE);
    ESP_LOGI(TAG, "failed to upload image.");
    g_is_img_on = false;
    return ret;
}
static inline esp_err_t edit_conf(const char *d)
{
    parse_conf_line(d);
    esp_err_t e = write_config();
    if (e != ESP_OK)
    {
        ESP_RETURN_ON_FALSE(esp_websocket_client_is_connected(ws_client) &&
                                (-1 != esp_websocket_client_send_bin(ws_client,
                                                                     CTRL_CODE + S_E_SET_CONF,
                                                                     1,
                                                                     pdMS_TO_TICKS(50))),
                            ESP_FAIL,
                            TAG,
                            "Failed to send set_conf error signal to server.");
    }
    ESP_RETURN_ON_FALSE(esp_websocket_client_is_connected(ws_client) &&
                            (-1 != esp_websocket_client_send_bin(ws_client,
                                                                 CTRL_CODE + S_S_SET_CONF,
                                                                 1,
                                                                 pdMS_TO_TICKS(50))),
                        ESP_FAIL,
                        TAG,
                        "Failed to send set_conf success signal to server.");
    ESP_LOGI(TAG, "SET_CONF_DONE");
    return ESP_OK;
}
// functions above are for data processing callback

// data processing callback
static void ws_data_cb(void *ev_arg, esp_event_base_t ev_base, int32_t ev_id, void *ev_data)
{
    esp_websocket_event_data_t *data = (esp_websocket_event_data_t *)ev_data;
    if (!g_is_img_on && data->op_code == 0x02 && data->data_ptr)
    {
        switch (*(data->data_ptr))
        {
        case CTRL_CODE_PCM:
            if (g_is_rig_tx)
            {
                if (app_config.tx_limit_ms > 0 && xTaskGetTickCount() - last_ptt_on > pdMS_TO_TICKS(app_config.tx_limit_ms))
                {
                    ptt_off();
                    return;
                }
                size_t written_bytes = 0;
                pwm_audio_write(((uint8_t *)data->data_ptr) + 1, (data->data_len) - 1, &written_bytes, 1000 / portTICK_PERIOD_MS);
            }
            return;
        case CTRL_CODE_TX:
            last_ptt_on = xTaskGetTickCount();
            ptt_on();
            return;
        case CTRL_CODE_TX_STOP:
            ptt_off();
            return;
        case CTRL_CODE_IMG_GET:
            ESP_RETURN_VOID_ON_ERROR(get_and_upload_img(), TAG, "get_and_upload_img failed.");
            return;
        case CTRL_CODE_SET_CONF:
            ESP_RETURN_VOID_ON_ERROR(edit_conf(data->data_ptr + 1), TAG, "edit_conf failed.");
            return;
        case CTRL_CODE_RESET:
            esp_restart();
            return;
        case CTRL_CODE_VERIFY:
            if (data->data_len != 17)
                return;
            memcpy(random_verify, data->data_ptr + 1, 16);
            calculate_passkey();
            esp_websocket_client_send_bin(ws_client, (const char *)app_passkey, 16, pdMS_TO_TICKS(10)); // send key for verifying
            return;
        case CTRL_CODE_REFUSE:
            ESP_LOGE(TAG, "Server refused the connection.");
            xEventGroupSetBits(ws_event_group, WS_EVT_REFUSE_BIT);
            return;
        }
    }
}

// functions below are websocket (dis)connect event callbacks
static void ws_conn_cb(void *ev_arg, esp_event_base_t ev_base, int32_t ev_id, void *ev_data)
{
    led_indicator_stop(led_handle, BLINK_DISCONN);
    esp_websocket_client_send_bin(ws_client, (const char *)device_mac_address, strlen(device_mac_address), pdMS_TO_TICKS(10));
}
static void ws_disconn_cb(void *ev_arg, esp_event_base_t ev_base, int32_t ev_id, void *ev_data)
{
    led_indicator_start(led_handle, BLINK_DISCONN);
}
// functions above are websocket (dis)connect event callbacks

void ws_destroy_task(void *arg)
{
    EventBits_t bits = xEventGroupWaitBits(ws_event_group, WS_EVT_REFUSE_BIT, pdFALSE, pdFALSE, portMAX_DELAY);
    if (bits & WS_EVT_REFUSE_BIT)
    {
        esp_websocket_client_stop(ws_client);
        esp_websocket_client_destroy(ws_client);
        ESP_LOGW(TAG, "connection refused, websocket client closed.");
    }
    if (ws_adc_tx_task_handle)
        vTaskDelete(ws_adc_tx_task_handle);
    vTaskDelete(NULL);
}

// 初始化ws客户端
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
        .task_stack = 2 * WS_BUF_SIZE,
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
    xTaskCreatePinnedToCore(ws_destroy_task, "ws_destroy_task", 2 * 1024, NULL, 0, NULL, 1);
    return ESP_OK;
}

// adc录音-ws传输 主任务
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
    adc_continuous_data_t adc_read_buf[ADC_BUF_SIZE] = {0}; // 样本缓冲区
    int16_t ws_send_buf[ADC_BUF_SIZE + 1] = {0};            // 待发数据缓冲区
    uint32_t read_samples = 0;                              // 读取到的样本数
    esp_err_t err;
    bool ctrl_level;
    uint8_t ctrl_off_delay = 0;
    ESP_LOGI(TAG, "ws_adc_tx_task runs into mainloop.");
    for (;;)
    {
        while (g_is_img_on || g_is_rig_tx || !esp_websocket_client_is_connected(ws_client) || gpio_get_level(GPIO_CTRL) == RIG_CTRL_OFF)
        {
            vTaskDelay(pdMS_TO_TICKS(20)); // waiting
        }
        if (-1 == esp_websocket_client_send_bin(ws_client, CTRL_CODE + RX, 1, pdMS_TO_TICKS(50)))
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
            read_samples = 0;                                                                                                                             // reset count
            err = adc_continuous_read_parse(adc_handle, adc_read_buf, sizeof(adc_read_buf) / sizeof(adc_read_buf[0]), &read_samples, pdMS_TO_TICKS(100)); // read and parse
            if (ESP_OK == err)
            {
                memset(ws_send_buf, 0, sizeof(ws_send_buf)); // reset send buffer
                for (uint32_t i = 0; i < read_samples; ++i)
                {
                    if (adc_read_buf[i].valid)
                    {
                        ws_send_buf[i + 1] = ((int16_t)(adc_read_buf[i].raw_data) + app_config.adc_offset) << 4; // process samples
                    }
                }
                *(((char *)ws_send_buf) + 1) = CTRL_CODE[PCM];
                if (g_is_img_on || g_is_rig_tx || !esp_websocket_client_is_connected(ws_client))
                {
                    vTaskDelay(pdMS_TO_TICKS(100));
                    break;
                }
                if (-1 == esp_websocket_client_send_bin(ws_client, ((const char *)ws_send_buf) + 1, read_samples * 2 + 1, pdMS_TO_TICKS(100)))
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
        if (-1 == esp_websocket_client_send_bin(ws_client, CTRL_CODE + RX_STOP, 1, pdMS_TO_TICKS(50)))
        {
            ESP_LOGE(TAG, "Failed to send end code to server.");
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}
