#include "ws_callbacks.h"

#define TAG "WS_CB"

EventGroupHandle_t ws_event_group;
TickType_t last_ptt_on; // used for calculating tx time limit

QueueHandle_t ws_send_queue_handle;
QueueHandle_t ws_task_play_queue_handle;
QueueHandle_t ws_task_afsk_queue_handle;

volatile bool verified_client;

// functions below are for ws_state
static portMUX_TYPE ws_state_mux = portMUX_INITIALIZER_UNLOCKED;
static uint8_t ws_state = 0;
inline void ws_state_set(ws_state_t state, bool v)
{
    portENTER_CRITICAL(&ws_state_mux);
    if (v)
        ws_state |= state;
    else
        ws_state &= ~state;
    portEXIT_CRITICAL(&ws_state_mux);
}
inline bool ws_state_check(uint8_t v)
{
    bool ret;
    portENTER_CRITICAL(&ws_state_mux);
    ret = ((ws_state & v) == 0);
    portEXIT_CRITICAL(&ws_state_mux);
    return ret;
}
inline bool ws_state_idle(void)
{
    bool ret;
    portENTER_CRITICAL(&ws_state_mux);
    ret = (ws_state == 0);
    portEXIT_CRITICAL(&ws_state_mux);
    return ret;
}
// functions above are for ws_state

// functions below are for data processing callback
inline void ptt_on(void)
{
    last_pwm_write = last_ptt_on = xTaskGetTickCount();
    ws_state_set(WS_STAT_RX, true);
    ESP_LOGI(TAG, "RIG TX ON");
    led_indicator_start(led_handle, BLINK_RED);
    gpio_set_level(GPIO_PTT, RIG_PTT_ON);
}
inline void ptt_off(void)
{
    ws_state_set(WS_STAT_RX, false);
    ESP_LOGI(TAG, "RIG TX OFF");
    led_indicator_stop(led_handle, BLINK_RED);
    gpio_set_level(GPIO_PTT, RIG_PTT_OFF);
}
inline esp_err_t edit_conf(const char *d, size_t len)
{
    ws_state_set(WS_STAT_CFG, true);
    parse_conf_line(d, len);
    esp_err_t e = write_config();
    if (e != ESP_OK)
    {
        send_to_ws(NULL, 0, CTRL_CODE_S_E_SET_CONF);
        ESP_LOGE(TAG, "failed to set config.");
        ws_state_set(WS_STAT_CFG, false);
        return e;
    }
    send_to_ws(NULL, 0, CTRL_CODE_S_S_SET_CONF);
    ESP_LOGI(TAG, "SET CONF:");
    print_config();
    ESP_LOGI(TAG, "SET CONF DONE");
    ws_state_set(WS_STAT_CFG, false);
    return ESP_OK;
}
void play_pcm_task(void *arg)
{
    data_packet_t pkt;
    char *saveptr;
    char *name;
    char full_filename[128];
    char err_info[160];
    char read_buf[1024];
    ESP_LOGI(TAG, "play_pcm_task runs into mainloop.");
    for (;;)
    {
        if (xQueueReceive(ws_task_play_queue_handle, &pkt, portMAX_DELAY))
        {
            ws_state_set(WS_STAT_PLAY, true);
            char *filenames = heap_caps_malloc(pkt.len + 1, MALLOC_CAP_SPIRAM);
            if (!filenames)
            {
                ESP_LOGE(TAG, "cannot process PLAY packet: heap_caps_malloc failed.");
                send_to_ws("cannot process PLAY packet: heap_caps_malloc failed.", 52, CTRL_CODE_S_MESSAGE);
                goto err;
            }
            filenames[pkt.len] = '\0';
            memcpy(filenames, pkt.data, pkt.len);
            name = strtok_r(filenames, "/", &saveptr);
            ptt_on();
            memset(read_buf, 0, 1024);
            vTaskDelay(pdMS_TO_TICKS(300));
            send_to_queue(pwm_write_queue_handle, read_buf, 1024);
            vTaskDelay(pdMS_TO_TICKS(30));
            while (name != NULL)
            {
                sprintf(full_filename, MOUNT_POINT "/pcm/%s.pcm", name);
                FILE *f = NULL;
                f = fopen(full_filename, "r");
                if (!f)
                {
                    ESP_LOGE(TAG, "cannot open file %s", full_filename);
                    sprintf(err_info, "cannot open file %s", full_filename);
                    send_to_ws(err_info, strlen(err_info), CTRL_CODE_S_MESSAGE);
                    goto err;
                }
                size_t read_bytes = 0;
                uint8_t time_compensation_count = 0;
                while ((read_bytes = fread(read_buf, 1, sizeof(read_buf), f)))
                {
                    ++time_compensation_count;
                    send_to_queue(pwm_write_queue_handle, read_buf, read_bytes);
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
            // vTaskDelay(pdMS_TO_TICKS(100));
            // ptt_off();
            free(pkt.data);
            free(filenames);
            send_to_ws(NULL, 0, CTRL_CODE_S_S_PLAY);
            ESP_LOGI(TAG, "play sounds ok.");
            ws_state_set(WS_STAT_PLAY, false);
            continue;

        err:
            ptt_off();
            free(pkt.data);
            free(filenames);
            send_to_ws(NULL, 0, CTRL_CODE_S_E_PLAY);
            ESP_LOGI(TAG, "failed to play sounds.");
            ws_state_set(WS_STAT_PLAY, false);
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
    ESP_LOGI(TAG, "afsk_send_task runs into mainloop.");
    for (;;)
    {
        if (xQueueReceive(ws_task_afsk_queue_handle, &afsk_data, portMAX_DELAY))
        {
            ws_state_set(WS_STAT_AFSK, true);
            switch (*(afsk_data.data))
            {
            case AFSK_DATA_BIN:
                if (afsk_data.len < 3)
                {
                    ESP_LOGE(TAG, "invalid afsk data: data too short.");
                    send_to_ws("invalid afsk data: data too short.", 34, CTRL_CODE_S_MESSAGE);
                    send_to_ws(NULL, 0, CTRL_CODE_S_E_AFSK);
                    goto clean;
                }
                uint16_t bit_len = (uint16_t)(afsk_data.data[1]) + ((uint16_t)(afsk_data.data[2]) << 8);
                if (bit_len > (afsk_data.len - 3) * 8)
                {
                    ESP_LOGE(TAG, "invalid afsk data: binary stream too short.");
                    send_to_ws("invalid afsk data: binary stream too short.", 43, CTRL_CODE_S_MESSAGE);
                    send_to_ws(NULL, 0, CTRL_CODE_S_E_AFSK);
                    goto clean;
                }

                ptt_on();
                vTaskDelay(pdMS_TO_TICKS(500));
                afsk1200_to_pwm(afsk_data.data + 3, bit_len);
                // vTaskDelay(pdMS_TO_TICKS(300));
                // ptt_off();
                send_to_ws(NULL, 0, CTRL_CODE_S_S_AFSK);
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
                            send_to_ws("invalid afsk data: reset tocall.", 32, CTRL_CODE_S_MESSAGE);
                            send_to_ws(NULL, 0, CTRL_CODE_S_E_AFSK);
                            goto clean;
                        }
                        if (index + 7 > afsk_data.len)
                        {
                            ESP_LOGE(TAG, "invalid afsk data: broken tocall.");
                            send_to_ws("invalid afsk data: broken tocall.", 33, CTRL_CODE_S_MESSAGE);
                            send_to_ws(NULL, 0, CTRL_CODE_S_E_AFSK);
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
                            send_to_ws("invalid afsk data: invalid tocall ssid.", 39, CTRL_CODE_S_MESSAGE);
                            send_to_ws(NULL, 0, CTRL_CODE_S_E_AFSK);
                            goto clean;
                        }
                        aprs_address[6] = (afsk_data.data[index++] << 1) | 0b11100000;
                        set_tocall = true;
                        break;
                    case APRS_FIELD_FROMCALL:
                        if (set_fromcall)
                        {
                            ESP_LOGE(TAG, "invalid afsk data: reset fromcall.");
                            send_to_ws("invalid afsk data: reset fromcall.", 34, CTRL_CODE_S_MESSAGE);
                            send_to_ws(NULL, 0, CTRL_CODE_S_E_AFSK);
                            goto clean;
                        }
                        if (index + 7 > afsk_data.len)
                        {
                            ESP_LOGE(TAG, "invalid afsk data: broken fromcall.");
                            send_to_ws("invalid afsk data: broken fromcall.", 35, CTRL_CODE_S_MESSAGE);
                            send_to_ws(NULL, 0, CTRL_CODE_S_E_AFSK);
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
                            send_to_ws("invalid afsk data: invalid fromcall ssid.", 41, CTRL_CODE_S_MESSAGE);
                            send_to_ws(NULL, 0, CTRL_CODE_S_E_AFSK);
                            goto clean;
                        }
                        aprs_address[13] = (afsk_data.data[index++] << 1) | 0b01100000;
                        set_fromcall = true;
                        break;
                    case APRS_FIELD_PATH:
                        if (path_len >= 8)
                        {
                            ESP_LOGE(TAG, "invalid afsk data: too many path.");
                            send_to_ws("invalid afsk data: too many path.", 33, CTRL_CODE_S_MESSAGE);
                            send_to_ws(NULL, 0, CTRL_CODE_S_E_AFSK);
                            goto clean;
                        }
                        if (index + 7 > afsk_data.len)
                        {
                            ESP_LOGE(TAG, "invalid afsk data: broken path.");
                            send_to_ws("invalid afsk data: broken path.", 31, CTRL_CODE_S_MESSAGE);
                            send_to_ws(NULL, 0, CTRL_CODE_S_E_AFSK);
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
                            send_to_ws("invalid afsk data: invalid path ssid.", 37, CTRL_CODE_S_MESSAGE);
                            send_to_ws(NULL, 0, CTRL_CODE_S_E_AFSK);
                            goto clean;
                        }
                        aprs_address[13 + path_len * 7 + 7] = (afsk_data.data[index++] << 1) | 0b01100000;
                        ++path_len;
                        break;
                    case APRS_FIELD_DATA:
                        if (!set_fromcall)
                        {
                            ESP_LOGE(TAG, "invalid afsk data: no fromcall.");
                            send_to_ws("invalid afsk data: no fromcall.", 31, CTRL_CODE_S_MESSAGE);
                            send_to_ws(NULL, 0, CTRL_CODE_S_E_AFSK);
                            goto clean;
                        }
                        if (!set_tocall)
                        {
                            ESP_LOGE(TAG, "invalid afsk data: no tocall.");
                            send_to_ws("invalid afsk data: no tocall.", 29, CTRL_CODE_S_MESSAGE);
                            send_to_ws(NULL, 0, CTRL_CODE_S_E_AFSK);
                            goto clean;
                        }
                        if (index > afsk_data.len)
                        {
                            ESP_LOGE(TAG, "invalid afsk data: invalid data.");
                            send_to_ws("invalid afsk data: invalid data.", 32, CTRL_CODE_S_MESSAGE);
                            send_to_ws(NULL, 0, CTRL_CODE_S_E_AFSK);
                            goto clean;
                        }
                        aprs_address[13 + path_len * 7] |= 0b00000001;
                        aprs_raw_data_size = afsk_data.len - index + 18 + path_len * 7; // 18 =  2 crc + UI + PID + 14 addr
                        uint8_t *aprs_raw_data = heap_caps_malloc(aprs_raw_data_size, MALLOC_CAP_SPIRAM);
                        if (aprs_raw_data == NULL)
                        {
                            ESP_LOGE(TAG, "failed to modulate afsk: heap_caps_malloc failed.");
                            send_to_ws("failed to modulate afsk: heap_caps_malloc failed.", 49, CTRL_CODE_S_MESSAGE);
                            send_to_ws(NULL, 0, CTRL_CODE_S_E_AFSK);
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
                            send_to_ws("failed to modulate afsk: heap_caps_malloc failed.", 49, CTRL_CODE_S_MESSAGE);
                            send_to_ws(NULL, 0, CTRL_CODE_S_E_AFSK);
                            goto clean;
                        }
                        l = bit_stuff(aprs_raw_data, aprs_raw_data_size * 8, aprs_bits + 64);
                        l = add_frame_flag(aprs_bits, l);
                        nrzi_modulate(aprs_bits, l);
                        ptt_on();
                        vTaskDelay(pdMS_TO_TICKS(500));
                        afsk1200_to_pwm(aprs_bits, l);
                        // vTaskDelay(pdMS_TO_TICKS(300));
                        // ptt_off();

                        free(aprs_raw_data);
                        free(aprs_bits);

                        send_to_ws(NULL, 0, CTRL_CODE_S_S_AFSK);
                        goto clean;
                    default:
                        ESP_LOGE(TAG, "invalid afsk data: invalid field.");
                        send_to_ws("invalid afsk data: invalid field.", 33, CTRL_CODE_S_MESSAGE);
                        send_to_ws(NULL, 0, CTRL_CODE_S_E_AFSK);
                        goto clean;
                    }
                }
                goto clean;
            default:
                ESP_LOGE(TAG, "invalid afsk data type.");
                send_to_ws("invalid afsk data type.", 23, CTRL_CODE_S_MESSAGE);
                send_to_ws(NULL, 0, CTRL_CODE_S_E_AFSK);
                goto clean;
            }

        clean:
            // free
            free(afsk_data.data);
            // log
            ESP_LOGI(TAG, "AFSK1200 SEND DONE");
            ws_state_set(WS_STAT_AFSK, false);
        }
    }
}
// functions above are for data processing callback

// functions below are websocket (dis)connect event callbacks
void ws_conn_cb(void *ev_arg, esp_event_base_t ev_base, int32_t ev_id, void *ev_data)
{
    led_indicator_stop(led_handle, BLINK_DISCONN);
    ESP_LOGI(TAG, "connected to server.");
    verified_client = false;
    send_to_ws(device_mac_address, strlen(device_mac_address), CTRL_CODE_PASSTHROUGH);
}
void ws_disconn_cb(void *ev_arg, esp_event_base_t ev_base, int32_t ev_id, void *ev_data)
{
    verified_client = false;
    if (ws_state > (1 << WS_STAT_RX))
        ptt_off();
    led_indicator_start(led_handle, BLINK_DISCONN);
}
// functions above are websocket (dis)connect event callbacks
