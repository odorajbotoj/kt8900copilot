#include "gpio.h"

#define TAG "GPIO"

esp_err_t gpio_init(void)
{
    gpio_config_t cfg_ptt = {
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_OUTPUT_OD, // open-drain
        .pin_bit_mask = (1ULL << GPIO_PTT),
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .pull_up_en = GPIO_PULLUP_DISABLE,
    };
    gpio_config_t cfg_ctrl = {
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_INPUT,
        .pin_bit_mask = (1ULL << GPIO_CTRL),
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&cfg_ptt), TAG, "Failed to set PTT gpio.");
    gpio_set_level(GPIO_PTT, RIG_PTT_OFF);
    ESP_RETURN_ON_ERROR(gpio_config(&cfg_ctrl), TAG, "Failed to set CTRL gpio.");
    return ESP_OK;
}
