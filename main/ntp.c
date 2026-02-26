#include "ntp.h"

#define TAG "NTP"

void ntp_init(void)
{
    setenv("TZ", app_config.timezone, 1);
    tzset();
    esp_sntp_config_t ntp_config = ESP_NETIF_SNTP_DEFAULT_CONFIG(app_config.ntp_server);
    esp_netif_sntp_init(&ntp_config);
    esp_netif_sntp_start();
    esp_err_t e = ESP_OK;
    if (e = esp_netif_sntp_sync_wait(pdMS_TO_TICKS(10000)), e != ESP_OK)
    {
        ESP_LOGI(TAG, "Failed to update system time within 10s timeout, error: %s", esp_err_to_name(e));
    }
    time_t ct;
    time(&ct);
    struct tm *lt = localtime(&ct);
    ESP_LOGI(TAG, "Current time: %s", asctime(lt));
}
