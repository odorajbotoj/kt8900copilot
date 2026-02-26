#include "macros.h"

#include "utils.h"

#include "audio_adc.h"
#include "audio_pwm.h"
#include "gpio.h"
#include "led.h"
#include "sdcard.h"
#include "wifi.h"
#include "ws.h"

#include "tests.h"

#define TAG "MAIN"

static char wifi_ssid[128];
static char wifi_password[128];
static char server_addr[128];
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
    FILE *fptr = NULL;
    fptr = fopen(MOUNT_POINT "/netconf.txt", "r");
    if (fptr == NULL)
    {
        ESP_LOGE(TAG, "cannot read config file.");
        return;
    }
    memset(wifi_ssid, 0, sizeof(wifi_ssid));
    memset(wifi_password, 0, sizeof(wifi_password));
    memset(server_addr, 0, sizeof(server_addr));
    memset(ws_key, 0, sizeof(ws_key));
    fgets(wifi_ssid, 120, fptr);
    remove_right_space(wifi_ssid);
    fgets(wifi_password, 120, fptr);
    remove_right_space(wifi_password);
    fgets(server_addr, 120, fptr);
    remove_right_space(server_addr);
    fgets(ws_key, 120, fptr);
    remove_right_space(ws_key);
    fclose(fptr);
    ESP_LOGI(TAG, "read config:\nwifi ssid: %s\nwifi password length: %zu\nserver address: %s\nws key length: %zu", wifi_ssid, strlen(wifi_password), server_addr, strlen(ws_key));
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
    ESP_ERROR_CHECK(wifi_init(wifi_ssid, wifi_password));

    // adc
    ESP_ERROR_CHECK(adc_init());
    // pwm
    ESP_ERROR_CHECK(pwm_init());

    // websocket
    ESP_ERROR_CHECK(websocket_init(server_addr, cert_pem));

    // xTaskCreatePinnedToCore(ptt_ctrl_test, "ptt_ctrl_test", 1024, NULL, 0, NULL, 1);
    // xTaskCreatePinnedToCoreWithCaps(adc_audio_record_test, "adc_audio_test", 1024 * 1024, NULL, 0, NULL, 1, MALLOC_CAP_SPIRAM);
    // xTaskCreatePinnedToCore(pwm_audio_test, "pwm_audio_test", 4096, NULL, 0, NULL, 0);
    xTaskCreatePinnedToCoreWithCaps(ws_adc_tx_task, "ws_adc_tx_task", 4 * 1024 * 1024, NULL, 0, NULL, 1, MALLOC_CAP_SPIRAM);

    led_indicator_stop(led_handle, BLINK_YELLOW_3);
}
