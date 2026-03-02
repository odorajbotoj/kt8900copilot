#include "esp_check.h"

#include "utils.h"

#include "config.h"

#include "led.h"
#include "gpio.h"
#include "sdcard.h"
#include "camera.h"
#include "wifi.h"
#include "ntp.h"
#include "audio_adc.h"
#include "audio_pwm.h"
#include "ws.h"

#define TAG "MAIN"

void app_main(void)
{
    esp_err_t ret = ESP_OK;

    // welcome
    get_mac();
    ESP_LOGI(TAG, "kt8900copilot - BG4QBF\n****************\ndevice MAC address: %s\n****************", device_mac_address);
    // led
    ESP_ERROR_CHECK(led_init());
    led_indicator_start(led_handle, BLINK_OFF);
    led_indicator_start(led_handle, BLINK_YELLOW_3);

    // gpio
    ESP_GOTO_ON_ERROR(gpio_init(), err, TAG, "gpio_init failed.");

    // sdcard
    ESP_GOTO_ON_ERROR(sdcard_init(), err, TAG, "sdcard_init failed.");
    // read config
    ESP_GOTO_ON_ERROR(load_config(), err, TAG, "load_config failed.");
    print_config();

    // camera
    ESP_GOTO_ON_ERROR(camera_init(), err, TAG, "camera_init failed.");

    // wifi
    ESP_GOTO_ON_ERROR(nvs_init(), err, TAG, "nvs_init failed.");
    ESP_GOTO_ON_ERROR(wifi_init(), err, TAG, "wifi_init failed.");
    ntp_init();

    // adc
    ESP_GOTO_ON_ERROR(adc_init(), err, TAG, "adc_init failed.");
    // pwm
    ESP_GOTO_ON_ERROR(pwm_init(), err, TAG, "pwm_init failed.");

    // websocket
    ESP_GOTO_ON_ERROR(websocket_init(), err, TAG, "websocket_init failed.");

    xTaskCreatePinnedToCoreWithCaps(ws_adc_tx_task, "ws_adc_tx_task", 4 * 1024 * 1024, NULL, 0, &ws_adc_tx_task_handle, 1, MALLOC_CAP_SPIRAM);

    led_indicator_stop(led_handle, BLINK_YELLOW_3);
    return;

err:
    led_indicator_start(led_handle, BLINK_ERROR);
    ESP_LOGE(TAG, "init error (%s), main function exiting ...", esp_err_to_name(ret));
    return;
}
