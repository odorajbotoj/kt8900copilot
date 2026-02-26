#include "tests.h"

#include "pwm_audio.h"
#include "freertos/FreeRTOS.h"

#include "audio_adc.h"
#include "gpio.h"

#include <math.h>

#define TAG "TESTS"

void pwm_audio_test(void *arg)
{
    int16_t test_sound[1024] = {0};
    size_t written_bytes;
    for (int i = 0; i < 1024; ++i)
    {
        test_sound[i] = (int16_t)(sin(i * 0.1963495) * 32767);
    }
    while (1)
    {
        written_bytes = 0;
        pwm_audio_write((uint8_t *)test_sound, sizeof(test_sound), &written_bytes, 1000 / portTICK_PERIOD_MS);
    }
}

void adc_audio_record_test(void *arg)
{
    ESP_LOGI(TAG, "adc_audio_record_test start");
    adc_continuous_data_t adc_read_buf[ADC_BUF_SIZE / 4] = {0}; // 样本缓冲区
    int16_t filebuf[ADC_BUF_SIZE / 4] = {0};
    uint32_t read_samples = 0; // 读取到的样本数
    esp_err_t err;
    int cnt = 0;
    char name[32];

    while (1)
    {
        while (gpio_get_level(GPIO_CTRL) == RIG_CTRL_OFF)
        {
            vTaskDelay(pdMS_TO_TICKS(20));
        }
        sprintf(name, "/sdcard/%d.pcm", cnt);
        FILE *f = fopen(name, "wb");
        assert(f);
        while (gpio_get_level(GPIO_CTRL) == RIG_CTRL_ON)
        {
            read_samples = 0;                                                                                          // 重置读取计数
            err = adc_continuous_read_parse(adc_handle, adc_read_buf, ADC_BUF_SIZE / 4, &read_samples, portMAX_DELAY); // 读取并解析
            if (ESP_OK == err)
            {
                memset(filebuf, 0, sizeof(filebuf));
                for (uint32_t i = 0; i < read_samples; ++i)
                {
                    if (adc_read_buf[i].valid)
                    {
                        filebuf[i] = ((int16_t)(adc_read_buf[i].raw_data) - 1800) << 4; // 处理样本
                    }
                }
                fwrite(filebuf, sizeof(filebuf[0]), sizeof(filebuf) / sizeof(filebuf[0]), f);
            }
        }
        fclose(f);
        ESP_LOGI(TAG, "record: %d", cnt);
        ++cnt;
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    vTaskDelete(NULL);
}

void ptt_ctrl_test(void *arg)
{
    while (1)
    {
        if (gpio_get_level(GPIO_CTRL) == RIG_CTRL_ON)
        {
            gpio_set_level(GPIO_PTT, RIG_PTT_ON);
            vTaskDelay(pdMS_TO_TICKS(3000));
            gpio_set_level(GPIO_PTT, RIG_PTT_OFF);
            ESP_LOGI(TAG, "ptt");
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}
