#include "ws.h"

#define TAG "WS"

esp_websocket_client_handle_t ws_client;

volatile ws_state_t ws_state;

TaskHandle_t ws_send_task_handle;
TaskHandle_t rig_tx_watchdog_handle;
TaskHandle_t get_and_upload_img_task_handle;
TaskHandle_t play_pcm_task_handle;

QueueHandle_t ws_send_queue_handle;

static EventGroupHandle_t ws_event_group;
#define WS_EVT_REFUSE_BIT BIT0

static TickType_t last_ptt_on; // used for calculating tx time limit

// functions below are for data processing callback
static inline void ptt_on(void)
{
    ws_state = WS_STAT_TX;
    ESP_LOGI(TAG, "RIG TX ON");
    led_indicator_start(led_handle, BLINK_RED);
    gpio_set_level(GPIO_PTT, RIG_PTT_ON);
}
static inline void ptt_off(void)
{
    ws_state = WS_STAT_IDLE;
    ESP_LOGI(TAG, "RIG TX OFF");
    led_indicator_stop(led_handle, BLINK_RED);
    gpio_set_level(GPIO_PTT, RIG_PTT_OFF);
}
void get_and_upload_img_task(void *arg)
{
    esp_err_t ret = ESP_OK;
    camera_fb_t *fb = NULL;
    if (!app_config.enable_cam)
    {
        send_to_queue(ws_send_queue_handle, NULL, 0, CTRL_CODE_S_E_CAM_DISABLED);
        ESP_LOGE(TAG, "camera is not enabled.");
        ret = ESP_FAIL;
        goto err;
    }
    ws_state = WS_STAT_IMG;
    led_indicator_start(led_handle, BLINK_WHITE);
    // refresh buffer
    esp_camera_fb_return(esp_camera_fb_get());
    // acquire a frame
    fb = esp_camera_fb_get();
    if (fb == NULL)
    {
        send_to_queue(ws_send_queue_handle, NULL, 0, CTRL_CODE_S_E_IMG_NIL);
        ESP_GOTO_ON_FALSE(fb, ESP_FAIL, err, TAG, "Camera Capture Failed");
        goto err;
    }
    // begin
    send_to_queue(ws_send_queue_handle, NULL, 0, CTRL_CODE_IMG_UPLOAD);
    // sending
    char buf[WS_BUF_SIZE] = {CTRL_CODE_IMG, 0};
    for (size_t i = 0; i < fb->len; i += WS_BUF_SIZE - 1)
    {
        size_t valid_len = fb->len - i >= WS_BUF_SIZE - 1 ? WS_BUF_SIZE - 1 : fb->len - i;
        memcpy(buf + 1, (fb->buf) + i, valid_len);
        send_to_queue(ws_send_queue_handle, buf, valid_len + 1, 0);
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    // end
    send_to_queue(ws_send_queue_handle, NULL, 0, CTRL_CODE_IMG_UPLOAD_STOP);
    // free
    esp_camera_fb_return(fb);
    led_indicator_stop(led_handle, BLINK_WHITE);
    ESP_LOGI(TAG, "IMG UPLOADED");
    ws_state = WS_STAT_IDLE;
    vTaskDelete(NULL);
err:
    if (fb)
        esp_camera_fb_return(fb);
    led_indicator_stop(led_handle, BLINK_WHITE);
    ESP_LOGE(TAG, "failed to upload image. error: %s", esp_err_to_name(ret));
    ws_state = WS_STAT_IDLE;
    vTaskDelete(NULL);
}
static inline esp_err_t edit_conf(const char *d, size_t len)
{
    ws_state = WS_STAT_CFG;
    parse_conf_line(d, len);
    esp_err_t e = write_config();
    if (e != ESP_OK)
    {
        send_to_queue(ws_send_queue_handle, NULL, 0, CTRL_CODE_S_E_SET_CONF);
        ESP_LOGE(TAG, "failed to set config.");
        ws_state = WS_STAT_IDLE;
        return e;
    }
    send_to_queue(ws_send_queue_handle, NULL, 0, CTRL_CODE_S_S_SET_CONF);
    ESP_LOGI(TAG, "SET CONF DONE");
    ws_state = WS_STAT_IDLE;
    return ESP_OK;
}
void play_pcm_task(void *arg)
{
    esp_err_t ret = ESP_OK;
    char *filenames = (char *)arg;
    char *saveptr;
    char *name = strtok_r(filenames, "/", &saveptr);
    char full_filename[128];
    strcpy(full_filename, MOUNT_POINT "/");
    last_ptt_on = xTaskGetTickCount();
    ptt_on();
    vTaskDelay(pdMS_TO_TICKS(400));
    while (name != NULL)
    {
        sprintf(full_filename, MOUNT_POINT "/pcm/%s.pcm", name);
        FILE *f = NULL;
        ESP_GOTO_ON_FALSE(f = fopen(full_filename, "r"), ESP_FAIL, err, TAG, "cannot open file %s", full_filename);
        char read_buf[1024];
        size_t read_bytes = 0;
        while ((read_bytes = fread(read_buf, 1, sizeof(read_buf), f)))
        {
            send_to_queue(pwm_write_queue_handle, read_buf, read_bytes, 0);
            vTaskDelay(pdMS_TO_TICKS(30));
        }
        fclose(f);
        name = strtok_r(NULL, "/", &saveptr);
    }
    vTaskDelay(pdMS_TO_TICKS(100));
    ptt_off();
    free(arg);
    send_to_queue(ws_send_queue_handle, NULL, 0, CTRL_CODE_S_S_PLAY);
    ESP_LOGI(TAG, "play sounds ok.");
    vTaskDelete(NULL);
    return;

err:
    ptt_off();
    free(arg);
    send_to_queue(ws_send_queue_handle, NULL, 0, CTRL_CODE_S_E_PLAY);
    ESP_LOGI(TAG, "failed to play sounds: %s", esp_err_to_name(ret));
    vTaskDelete(NULL);
    return;
}
// functions above are for data processing callback

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
            last_ptt_on = xTaskGetTickCount();
            ptt_on();
            return;
        case CTRL_CODE_TX_STOP:
            ptt_off();
            return;
        case CTRL_CODE_PLAY:
            char *filenames = malloc(data->data_len);
            filenames[data->data_len - 1] = '\0';
            memcpy(filenames, data->data_ptr + 1, data->data_len - 1);
            xTaskCreatePinnedToCoreWithCaps(play_pcm_task,
                                            "play_pcm_task",
                                            16 * 1024,
                                            filenames,
                                            2,
                                            &play_pcm_task_handle,
                                            1,
                                            MALLOC_CAP_SPIRAM);
            return;
        case CTRL_CODE_IMG_GET:
            xTaskCreatePinnedToCoreWithCaps(get_and_upload_img_task,
                                            "get_and_upload_img_task",
                                            1024 * 1024,
                                            NULL,
                                            2,
                                            &get_and_upload_img_task_handle,
                                            1,
                                            MALLOC_CAP_SPIRAM);
            return;
        case CTRL_CODE_SET_CONF:
            ESP_RETURN_VOID_ON_ERROR(edit_conf(data->data_ptr + 1, data->data_len - 1), TAG, "edit_conf failed.");
            return;
        case CTRL_CODE_RESET:
            esp_restart();
            return;
        case CTRL_CODE_VERIFY:
            if (data->data_len != 17)
                return;
            memcpy(random_verify, data->data_ptr + 1, 16);
            calculate_passkey();
            send_to_queue(ws_send_queue_handle, app_passkey, 16, 0);
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

// functions below are websocket (dis)connect event callbacks
static void ws_conn_cb(void *ev_arg, esp_event_base_t ev_base, int32_t ev_id, void *ev_data)
{
    led_indicator_stop(led_handle, BLINK_DISCONN);
    send_to_queue(ws_send_queue_handle, device_mac_address, strlen(device_mac_address), 0);
}
static void ws_disconn_cb(void *ev_arg, esp_event_base_t ev_base, int32_t ev_id, void *ev_data)
{
    if (get_and_upload_img_task_handle)
        vTaskDelete(get_and_upload_img_task_handle);
    if (play_pcm_task_handle)
        vTaskDelete(play_pcm_task_handle);
    if (ws_state > WS_STAT_RX)
        ptt_off();
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
    if (adc_read_task_handle)
        vTaskDelete(adc_read_task_handle);
    if (get_and_upload_img_task_handle)
        vTaskDelete(get_and_upload_img_task_handle);
    if (play_pcm_task_handle)
        vTaskDelete(play_pcm_task_handle);
    if (ws_send_task_handle)
        vTaskDelete(ws_send_task_handle);
    if (pwm_write_task_handle)
        vTaskDelete(pwm_write_task_handle);
    if (rig_tx_watchdog_handle)
        vTaskDelete(rig_tx_watchdog_handle);
    vTaskDelete(NULL);
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
    return ESP_OK;
}

void rig_tx_watchdog(void *arg)
{
    for (;;)
    {
        if (ws_state == WS_STAT_TX && app_config.tx_limit_ms > 0 && xTaskGetTickCount() - last_ptt_on > pdMS_TO_TICKS(app_config.tx_limit_ms))
        {
            ptt_off();
        }
        vTaskDelay(pdMS_TO_TICKS(100));
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
            if (esp_websocket_client_is_connected(ws_client))
            {
                if (!pkt.code)
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
