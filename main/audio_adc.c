#include "audio_adc.h"

#define TAG "AUDIO_ADC"

adc_continuous_handle_t adc_handle;
TaskHandle_t adc_read_task_handle;

esp_err_t adc_init(void)
{
    adc_continuous_handle_cfg_t adc_handle_cfg = {
        .max_store_buf_size = 2 * ADC_BUF_SIZE,
        .conv_frame_size = ADC_BUF_SIZE,
        .flags = {
            .flush_pool = true,
        },
    };
    ESP_RETURN_ON_ERROR(adc_continuous_new_handle(&adc_handle_cfg, &adc_handle), TAG, "Failed to new ADC handle.");

    adc_digi_pattern_config_t pattern_cfg[1] = {
        {
            .atten = ADC_ATTEN_DB_6,
            .channel = ADC_CHANNEL_0,
            .unit = ADC_UNIT_1,
            .bit_width = SOC_ADC_DIGI_MAX_BITWIDTH,
        },
    };
    adc_continuous_config_t adc_cfg = {
        .sample_freq_hz = 16000,
        .pattern_num = 1,
        .adc_pattern = pattern_cfg,
        .conv_mode = ADC_CONV_SINGLE_UNIT_1,
        .format = ADC_DIGI_OUTPUT_FORMAT_TYPE2,
    };
    ESP_RETURN_ON_ERROR(adc_continuous_config(adc_handle, &adc_cfg), TAG, "Failed to set ADC config.");

    ESP_RETURN_ON_ERROR(adc_continuous_start(adc_handle), TAG, "Failed to start ADC.");

    return ESP_OK;
}

void adc_read_task(void *arg)
{
    adc_continuous_data_t adc_read_buf[ADC_BUF_SIZE] = {0}; // 样本缓冲区
    int16_t ws_send_buf[ADC_BUF_SIZE] = {0};                // 待发数据缓冲区
    uint32_t read_samples = 0;                              // 读取到的样本数
    esp_err_t err;
    bool ctrl_level;
    uint8_t ctrl_off_delay = 0;
    ESP_LOGI(TAG, "adc_read_task runs into mainloop.");
    for (;;)
    {
        while (gpio_get_level(GPIO_CTRL) == RIG_CTRL_OFF || !ws_state_idle() || !esp_websocket_client_is_connected(ws_client))
        {
            vTaskDelay(pdMS_TO_TICKS(20)); // waiting
        }
        send_to_ws(NULL, 0, CTRL_CODE_RX);
        ws_state_set(WS_STAT_TX, true);
        ESP_LOGI(TAG, "RIG RX ON");
        led_indicator_start(led_handle, BLINK_GREEN);
        ctrl_level = gpio_get_level(GPIO_CTRL) == RIG_CTRL_ON;
        while (ctrl_level || ctrl_off_delay)
        {
            if (ctrl_level)
            {
                ctrl_off_delay = 16;
            }
            err = adc_continuous_read_parse(adc_handle, adc_read_buf, sizeof(adc_read_buf) / sizeof(adc_read_buf[0]), &read_samples, pdMS_TO_TICKS(100)); // read and parse
            read_samples = ADC_BUF_SIZE < read_samples ? ADC_BUF_SIZE : read_samples;
            if (ESP_OK == err)
            {
                for (uint32_t i = 0; i < read_samples; ++i)
                {
                    if (adc_read_buf[i].valid)
                    {
                        ws_send_buf[i] = ((int16_t)(adc_read_buf[i].raw_data) + app_config.adc_offset) << 4; // process samples
                    }
                }
                if (!ws_state_check(WS_STAT_RX | WS_STAT_AFSK | WS_STAT_PLAY | WS_STAT_CFG) || !esp_websocket_client_is_connected(ws_client))
                {
                    vTaskDelay(pdMS_TO_TICKS(100));
                    break;
                }
                send_to_ws(ws_send_buf, read_samples * 2, CTRL_CODE_PCM);
            }
            --ctrl_off_delay;
            ctrl_level = gpio_get_level(GPIO_CTRL) == RIG_CTRL_ON;
        }
        led_indicator_stop(led_handle, BLINK_GREEN);
        ESP_LOGI(TAG, "RIG RX OFF");
        ws_state_set(WS_STAT_TX, false);
        send_to_ws(NULL, 0, CTRL_CODE_RX_STOP);
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}
