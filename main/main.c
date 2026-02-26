#include "macros.h"

#include "utils.h"

#include "config.h"

#include "audio_adc.h"
#include "audio_pwm.h"
#include "gpio.h"
#include "led.h"
#include "sdcard.h"
#include "wifi.h"
#include "ws.h"

#define TAG "MAIN"

static char ws_key[128];
static char cert_pem[4096];

void app_main(void)
{
    // led
    ESP_ERROR_CHECK(led_init());
    led_indicator_start(led_handle, BLINK_OFF);
    led_indicator_start(led_handle, BLINK_YELLOW_3);

    // gpio
    ESP_ERROR_CHECK(gpio_init());

    // sdcard
    ESP_ERROR_CHECK(sdcard_init());
    // read config
    ESP_ERROR_CHECK(load_config());
    ESP_LOGI(TAG, "read config:\nwifi ssid: %s\nwifi password length: %zu\nserver address: %s\nws key length: %zu", app_config.wifi_ssid, strlen(app_config.wifi_password), app_config.server_addr, strlen(ws_key));
    // load cert.pem
    FILE *certptr = NULL;
    certptr = fopen(MOUNT_POINT "/cert.pem", "r");
    if (certptr == NULL)
    {
        ESP_LOGE(TAG, "cannot read cert.pem");
        return;
    }
    fread(cert_pem, sizeof(cert_pem[0]), sizeof(cert_pem) / sizeof(cert_pem[0]), certptr);
    fclose(certptr);
    ESP_LOGI(TAG, "read cert.pem:\nlength: %zu", strlen(cert_pem));

    // wifi
    ESP_ERROR_CHECK(nvs_init());
    ESP_ERROR_CHECK(wifi_init());

    // adc
    ESP_ERROR_CHECK(adc_init());
    // pwm
    ESP_ERROR_CHECK(pwm_init());

    // websocket
    ESP_ERROR_CHECK(websocket_init(cert_pem));

    // xTaskCreatePinnedToCore(ptt_ctrl_test, "ptt_ctrl_test", 1024, NULL, 0, NULL, 1);
    // xTaskCreatePinnedToCoreWithCaps(adc_audio_record_test, "adc_audio_test", 1024 * 1024, NULL, 0, NULL, 1, MALLOC_CAP_SPIRAM);
    // xTaskCreatePinnedToCore(pwm_audio_test, "pwm_audio_test", 4096, NULL, 0, NULL, 0);
    xTaskCreatePinnedToCoreWithCaps(ws_adc_tx_task, "ws_adc_tx_task", 4 * 1024 * 1024, NULL, 0, NULL, 1, MALLOC_CAP_SPIRAM);

    led_indicator_stop(led_handle, BLINK_YELLOW_3);
}
