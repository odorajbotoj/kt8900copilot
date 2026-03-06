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
    if (app_config.enable_cam)
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
    ESP_GOTO_ON_ERROR(esp_websocket_client_start(ws_client), err, TAG, "cannot start websocket client.");

    ws_send_queue_handle = xQueueCreate(16, sizeof(data_packet_t));
    ESP_GOTO_ON_FALSE(ws_send_queue_handle, ESP_FAIL, err, TAG, "failed to create ws_send_queue_handle.");
    pwm_write_queue_handle = xQueueCreate(16, sizeof(data_packet_t));
    ESP_GOTO_ON_FALSE(pwm_write_queue_handle, ESP_FAIL, err, TAG, "failed to create pwm_write_queue_handle.");

    xTaskCreatePinnedToCoreWithCaps(ws_send_task, "ws_send_task", 1024 * 1024, NULL, 1, &ws_send_task_handle, 1, MALLOC_CAP_SPIRAM);
    xTaskCreatePinnedToCoreWithCaps(rig_tx_watchdog, "rig_tx_watchdog", 2 * 1024, NULL, 2, &ws_send_task_handle, 1, MALLOC_CAP_SPIRAM);
    xTaskCreatePinnedToCoreWithCaps(adc_read_task, "adc_read_task", 1024 * 1024, NULL, 1, &adc_read_task_handle, 1, MALLOC_CAP_SPIRAM);
    xTaskCreatePinnedToCoreWithCaps(pwm_write_task, "pwm_write_task", 1024 * 1024, NULL, 1, &pwm_write_task_handle, 1, MALLOC_CAP_SPIRAM);

    led_indicator_stop(led_handle, BLINK_YELLOW_3);
    led_indicator_start(led_handle, BLINK_DISCONN);

    while (!esp_websocket_client_is_connected(ws_client))
    {
        vTaskDelay(pdMS_TO_TICKS(1000)); // waiting...
    }

    return;

err:
    led_indicator_start(led_handle, BLINK_ERROR);
    ESP_LOGE(TAG, "init error (%s), main function exiting ...", esp_err_to_name(ret));
    return;
}
