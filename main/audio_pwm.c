#include "audio_pwm.h"

#define TAG "AUDIO_PWM"

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
    ESP_RETURN_ON_ERROR(pwm_audio_set_param(32000, 16, 1), TAG, "Failed to set pwm audio.");
    ESP_RETURN_ON_ERROR(pwm_audio_start(), TAG, "Failed to start pwm audio.");

    return ESP_OK;
}
