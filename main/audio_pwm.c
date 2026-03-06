#include "audio_pwm.h"

#define TAG "AUDIO_PWM"

TaskHandle_t pwm_write_task_handle;
QueueHandle_t pwm_write_queue_handle;

esp_err_t pwm_init(void)
{
    pwm_audio_config_t pwm_conf = {
        .duty_resolution = LEDC_TIMER_10_BIT,
        .gpio_num_left = GPIO_NUM_19,
        .gpio_num_right = GPIO_NUM_NC,
        .ledc_channel_left = LEDC_CHANNEL_0,
        .ledc_channel_right = LEDC_CHANNEL_1,
        .ledc_timer_sel = LEDC_TIMER_0,
        .ringbuf_len = PWM_BUF_SIZE,
    };

    ESP_RETURN_ON_ERROR(pwm_audio_init(&pwm_conf), TAG, "Failed to init pwm audio.");
    ESP_RETURN_ON_ERROR(pwm_audio_set_param(16000, 16, 1), TAG, "Failed to set pwm audio.");
    ESP_RETURN_ON_ERROR(pwm_audio_start(), TAG, "Failed to start pwm audio.");

    return ESP_OK;
}

void pwm_write_task(void *arg)
{
    data_packet_t pkt;
    size_t written_bytes = 0;
    ESP_LOGI(TAG, "pwm_write_task runs into mainloop.");
    for (;;)
    {
        if (xQueueReceive(pwm_write_queue_handle, &pkt, portMAX_DELAY))
        {
            if (esp_websocket_client_is_connected(ws_client))
                pwm_audio_write(pkt.data, pkt.len, &written_bytes, portMAX_DELAY);
            free(pkt.data);
        }
    }
}
