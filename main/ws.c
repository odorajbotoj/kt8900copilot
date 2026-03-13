#include "ws.h"

#define TAG "WS"

esp_websocket_client_handle_t ws_client;

volatile ws_state_t ws_state;

TaskHandle_t ws_send_task_handle;
TaskHandle_t rig_tx_watchdog_handle;
TaskHandle_t get_and_upload_img_task_handle;
TaskHandle_t play_pcm_task_handle;
TaskHandle_t afsk_send_task_handle;

QueueHandle_t ws_send_queue_handle;
QueueHandle_t ws_task_play_queue_handle;
QueueHandle_t ws_task_afsk_queue_handle;

static EventGroupHandle_t ws_event_group;
#define WS_EVT_REFUSE_BIT BIT0
#define WS_EVT_CALL_IMG_BIT BIT1

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
    EventBits_t bits;
    esp_err_t ret;
    camera_fb_t *fb = NULL;
    char buf[WS_BUF_SIZE] = {CTRL_CODE_IMG, 0};
    for (;;)
    {
        bits = xEventGroupWaitBits(ws_event_group, WS_EVT_CALL_IMG_BIT, pdTRUE, pdFALSE, portMAX_DELAY);
        if (bits & WS_EVT_CALL_IMG_BIT)
        {
            ret = ESP_OK;
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
            ws_state = WS_STAT_IDLE;
            ESP_LOGI(TAG, "IMG UPLOADED");
            continue;
        err:
            if (fb)
                esp_camera_fb_return(fb);
            led_indicator_stop(led_handle, BLINK_WHITE);
            ws_state = WS_STAT_IDLE;
            ESP_LOGE(TAG, "failed to upload image. error: %s", esp_err_to_name(ret));
        }
    }
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
    data_packet_t pkt;
    esp_err_t ret;
    char *saveptr;
    char *name;
    char full_filename[128];
    for (;;)
    {
        if (xQueueReceive(ws_task_play_queue_handle, &pkt, portMAX_DELAY))
        {
            ret = ESP_OK;
            char *filenames = heap_caps_malloc(pkt.len + 1, MALLOC_CAP_SPIRAM);
            ESP_GOTO_ON_FALSE(filenames, ESP_FAIL, err, TAG, "cannot process PLAY packet: heap_caps_malloc failed.");
            filenames[pkt.len] = '\0';
            memcpy(filenames, pkt.data, pkt.len);
            name = strtok_r(filenames, "/", &saveptr);
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
                uint8_t time_compensation_count = 0;
                while ((read_bytes = fread(read_buf, 1, sizeof(read_buf), f)))
                {
                    ++time_compensation_count;
                    send_to_queue(pwm_write_queue_handle, read_buf, read_bytes, 0);
                    vTaskDelay(pdMS_TO_TICKS(30));
                    if (time_compensation_count > 5)
                    {
                        vTaskDelay(pdMS_TO_TICKS(10));
                        time_compensation_count = 0;
                    }
                }
                fclose(f);
                name = strtok_r(NULL, "/", &saveptr);
            }
            vTaskDelay(pdMS_TO_TICKS(100));
            ptt_off();
            free(pkt.data);
            free(filenames);
            send_to_queue(ws_send_queue_handle, NULL, 0, CTRL_CODE_S_S_PLAY);
            ESP_LOGI(TAG, "play sounds ok.");
            continue;

        err:
            ptt_off();
            free(pkt.data);
            free(filenames);
            send_to_queue(ws_send_queue_handle, NULL, 0, CTRL_CODE_S_E_PLAY);
            ESP_LOGI(TAG, "failed to play sounds: %s", esp_err_to_name(ret));
        }
    }
}
void afsk_send_task(void *arg)
{
    data_packet_t afsk_data;
    uint8_t aprs_address[70];
    uint8_t path_len;
    bool set_fromcall;
    bool set_tocall;
    size_t index;
    size_t aprs_raw_data_size;
    uint16_t crc;
    size_t l;
    for (;;)
    {
        if (xQueueReceive(ws_task_afsk_queue_handle, &afsk_data, portMAX_DELAY))
        {
            switch (*(afsk_data.data))
            {
            case AFSK_DATA_BIN:
                if (afsk_data.len < 3)
                {
                    ESP_LOGE(TAG, "invalid afsk data: data too short.");
                    goto clean;
                }
                uint16_t bit_len = (uint16_t)(afsk_data.data[1]) + ((uint16_t)(afsk_data.data[2]) << 8);
                if (bit_len > (afsk_data.len - 3) * 8)
                {
                    ESP_LOGE(TAG, "invalid afsk data: binary stream too short.");
                    goto clean;
                }

                last_ptt_on = xTaskGetTickCount();
                ptt_on();
                vTaskDelay(pdMS_TO_TICKS(500));
                afsk1200_to_pwm(afsk_data.data + 3, bit_len);
                vTaskDelay(pdMS_TO_TICKS(300));
                ptt_off();

                goto clean;
            case AFSK_DATA_APRS:
                // build aprs packet
                memset(aprs_address, 0, sizeof(aprs_address));
                path_len = 0;
                set_fromcall = false;
                set_tocall = false;
                index = 1;
                while (index < afsk_data.len)
                {
                    switch (afsk_data.data[index++])
                    {
                    case APRS_FIELD_TOCALL:
                        if (set_tocall)
                        {
                            ESP_LOGE(TAG, "invalid afsk data: reset tocall.");
                            goto clean;
                        }
                        if (index + 7 > afsk_data.len)
                        {
                            ESP_LOGE(TAG, "invalid afsk data: broken tocall.");
                            goto clean;
                        }
                        aprs_address[0] = afsk_data.data[index++] << 1;
                        aprs_address[1] = afsk_data.data[index++] << 1;
                        aprs_address[2] = afsk_data.data[index++] << 1;
                        aprs_address[3] = afsk_data.data[index++] << 1;
                        aprs_address[4] = afsk_data.data[index++] << 1;
                        aprs_address[5] = afsk_data.data[index++] << 1;
                        if (afsk_data.data[index] >= 16)
                        {
                            ESP_LOGE(TAG, "invalid afsk data: invalid tocall ssid.");
                            goto clean;
                        }
                        aprs_address[6] = (afsk_data.data[index++] << 1) | 0b11100000;
                        set_tocall = true;
                        break;
                    case APRS_FIELD_FROMCALL:
                        if (set_fromcall)
                        {
                            ESP_LOGE(TAG, "invalid afsk data: reset fromcall.");
                            goto clean;
                        }
                        if (index + 7 > afsk_data.len)
                        {
                            ESP_LOGE(TAG, "invalid afsk data: broken fromcall.");
                            goto clean;
                        }
                        aprs_address[7] = afsk_data.data[index++] << 1;
                        aprs_address[8] = afsk_data.data[index++] << 1;
                        aprs_address[9] = afsk_data.data[index++] << 1;
                        aprs_address[10] = afsk_data.data[index++] << 1;
                        aprs_address[11] = afsk_data.data[index++] << 1;
                        aprs_address[12] = afsk_data.data[index++] << 1;
                        if (afsk_data.data[index] >= 16)
                        {
                            ESP_LOGE(TAG, "invalid afsk data: invalid fromcall ssid.");
                            goto clean;
                        }
                        aprs_address[13] = (afsk_data.data[index++] << 1) | 0b01100000;
                        set_fromcall = true;
                        break;
                    case APRS_FIELD_PATH:
                        if (path_len >= 8)
                        {
                            ESP_LOGE(TAG, "invalid afsk data: too many path.");
                            goto clean;
                        }
                        if (index + 7 > afsk_data.len)
                        {
                            ESP_LOGE(TAG, "invalid afsk data: broken path.");
                            goto clean;
                        }
                        aprs_address[13 + path_len * 7 + 1] = afsk_data.data[index++] << 1;
                        aprs_address[13 + path_len * 7 + 2] = afsk_data.data[index++] << 1;
                        aprs_address[13 + path_len * 7 + 3] = afsk_data.data[index++] << 1;
                        aprs_address[13 + path_len * 7 + 4] = afsk_data.data[index++] << 1;
                        aprs_address[13 + path_len * 7 + 5] = afsk_data.data[index++] << 1;
                        aprs_address[13 + path_len * 7 + 6] = afsk_data.data[index++] << 1;
                        if (afsk_data.data[index] >= 16)
                        {
                            ESP_LOGE(TAG, "invalid afsk data: invalid path ssid.");
                            goto clean;
                        }
                        aprs_address[13 + path_len * 7 + 7] = (afsk_data.data[index++] << 1) | 0b01100000;
                        ++path_len;
                        break;
                    case APRS_FIELD_DATA:
                        if (!set_fromcall)
                        {
                            ESP_LOGE(TAG, "invalid afsk data: no fromcall.");
                            goto clean;
                        }
                        if (!set_tocall)
                        {
                            ESP_LOGE(TAG, "invalid afsk data: no tocall.");
                            goto clean;
                        }
                        if (index > afsk_data.len)
                        {
                            ESP_LOGE(TAG, "invalid afsk data: invalid data.");
                            goto clean;
                        }
                        aprs_address[13 + path_len * 7] |= 0b00000001;
                        aprs_raw_data_size = afsk_data.len - index + 18 + path_len * 7; // 18 =  2 crc + UI + PID + 14 addr
                        uint8_t *aprs_raw_data = heap_caps_malloc(aprs_raw_data_size, MALLOC_CAP_SPIRAM);
                        if (aprs_raw_data == NULL)
                        {
                            ESP_LOGE(TAG, "failed to modulate afsk: heap_caps_malloc failed.");
                            goto clean;
                        }
                        memcpy(aprs_raw_data, aprs_address, 14 + path_len * 7);                                   // copy header
                        aprs_raw_data[14 + path_len * 7] = 0x03;                                                  // UI Frame
                        aprs_raw_data[15 + path_len * 7] = 0xF0;                                                  // PID
                        memcpy(aprs_raw_data + 16 + path_len * 7, afsk_data.data + index, afsk_data.len - index); // copy data
                        crc = get_crc_16_ccitt_x25(aprs_raw_data, aprs_raw_data_size - 2);
                        aprs_raw_data[aprs_raw_data_size - 2] = crc & 0xFF;        // set crc lowbits
                        aprs_raw_data[aprs_raw_data_size - 1] = (crc >> 8) & 0xFF; // set crc highbits

                        uint8_t *aprs_bits = heap_caps_malloc(aprs_raw_data_size + aprs_raw_data_size / 5 + 64, MALLOC_CAP_SPIRAM);
                        if (aprs_bits == NULL)
                        {
                            free(aprs_raw_data);
                            ESP_LOGE(TAG, "failed to modulate afsk: heap_caps_malloc failed.");
                            goto clean;
                        }
                        l = bit_stuff(aprs_raw_data, aprs_raw_data_size * 8, aprs_bits + 64);
                        l = add_frame_flag(aprs_bits, l);
                        nrzi_modulate(aprs_bits, l);

                        last_ptt_on = xTaskGetTickCount();
                        ptt_on();
                        vTaskDelay(pdMS_TO_TICKS(500));
                        afsk1200_to_pwm(aprs_bits, l);
                        vTaskDelay(pdMS_TO_TICKS(300));
                        ptt_off();

                        free(aprs_raw_data);
                        free(aprs_bits);

                        goto clean;
                    default:
                        ESP_LOGE(TAG, "invalid afsk data: invalid field.");
                        goto clean;
                    }
                }
                goto clean;
            default:
                ESP_LOGE(TAG, "invalid afsk data type.");
                goto clean;
            }

        clean:
            // free
            free(afsk_data.data);
            // log
            ESP_LOGI(TAG, "AFSK1200 SEND DONE");
        }
    }
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
    ESP_LOGI(TAG, "connected to server.");
    send_to_queue(ws_send_queue_handle, device_mac_address, strlen(device_mac_address), 0);
}
static void ws_disconn_cb(void *ev_arg, esp_event_base_t ev_base, int32_t ev_id, void *ev_data)
{
    if (get_and_upload_img_task_handle && eTaskGetState(get_and_upload_img_task_handle) < eDeleted)
        vTaskDelete(get_and_upload_img_task_handle);
    if (play_pcm_task_handle && eTaskGetState(play_pcm_task_handle) < eDeleted)
        vTaskDelete(play_pcm_task_handle);
    if (afsk_send_task_handle && eTaskGetState(afsk_send_task_handle) < eDeleted)
        vTaskDelete(afsk_send_task_handle);
    if (ws_state > WS_STAT_RX)
        ptt_off();
    led_indicator_start(led_handle, BLINK_DISCONN);
}
// functions above are websocket (dis)connect event callbacks

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

    return ESP_OK;
}

void rig_tx_watchdog(void *arg)
{
    for (;;)
    {
        if (ws_state == WS_STAT_TX && app_config.tx_limit_ms > 0 && xTaskGetTickCount() - last_ptt_on > pdMS_TO_TICKS(app_config.tx_limit_ms))
        {
            ptt_off();
            ESP_LOGW(TAG, "rig_tx_watchdog set ptt_off.");
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
