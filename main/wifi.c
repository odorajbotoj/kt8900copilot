#include "wifi.h"

#define TAG "WIFI_CONN"

#define WIFI_CONNECTED_BIT BIT0

static EventGroupHandle_t wifi_event_group;

static void event_handler(void *arg, esp_event_base_t ev_base, int32_t ev_id, void *ev_data)
{
    if (ev_base == WIFI_EVENT)
    {
        if (ev_id == WIFI_EVENT_STA_START)
        {
            esp_wifi_connect();
        }
        else if (ev_id == WIFI_EVENT_STA_DISCONNECTED)
        {
            esp_wifi_connect();
            led_indicator_start(led_handle, BLINK_WIFI_DISCONN);
            ESP_LOGI(TAG, "retry to connect to the AP");
        }
    }
    else if (ev_base == IP_EVENT)
    {
        if (ev_id == IP_EVENT_STA_GOT_IP)
        {
            ip_event_got_ip_t *ev = (ip_event_got_ip_t *)ev_data;
            ESP_LOGI(TAG, "got ip:" IPSTR, IP2STR(&ev->ip_info.ip));
            led_indicator_stop(led_handle, BLINK_WIFI_DISCONN);
            xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
        }
    }
}

esp_err_t nvs_init(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_RETURN_ON_ERROR(nvs_flash_erase(), TAG, "Failed to erase NVS Flash.");
        ESP_RETURN_ON_ERROR(nvs_flash_init(), TAG, "Failed to init NVS Flash.");
    }
    return ESP_OK;
}

esp_err_t wifi_init(void)
{
    // init
    wifi_event_group = xEventGroupCreate();
    ESP_RETURN_ON_ERROR(esp_netif_init(), TAG, "Failed to init NETIF.");
    ESP_RETURN_ON_ERROR(esp_event_loop_create_default(), TAG, "Failed to create EventLoop.");
    esp_netif_create_default_wifi_sta();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&cfg), TAG, "Failed to init WiFi.");
    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_RETURN_ON_ERROR(esp_event_handler_instance_register(WIFI_EVENT,
                                                            ESP_EVENT_ANY_ID,
                                                            &event_handler,
                                                            NULL,
                                                            &instance_any_id),
                        TAG,
                        "Failed to register WiFi event callback.");
    ESP_RETURN_ON_ERROR(esp_event_handler_instance_register(IP_EVENT,
                                                            IP_EVENT_STA_GOT_IP,
                                                            &event_handler,
                                                            NULL,
                                                            &instance_got_ip),
                        TAG,
                        "Failed to register WiFi event callback.");
    // set ssid and password
    wifi_config_t wifi_config = {0};
    memcpy(wifi_config.sta.ssid, app_config.wifi_ssid, sizeof(wifi_config.sta.ssid));
    memcpy(wifi_config.sta.password, app_config.wifi_password, sizeof(wifi_config.sta.password));
    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), TAG, "Failed to set WiFi mode.");
    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_STA, &wifi_config), TAG, "Failed to set WiFi config.");
    // start
    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "Failed to start WiFi.");
    esp_wifi_set_ps(WIFI_PS_NONE);
    ESP_LOGI(TAG, "wifi_init_sta finished.");
    // wait for result
    EventBits_t bits = xEventGroupWaitBits(wifi_event_group,
                                           WIFI_CONNECTED_BIT,
                                           pdFALSE,
                                           pdFALSE,
                                           portMAX_DELAY);
    if (bits & WIFI_CONNECTED_BIT)
    {
        ESP_LOGI(TAG, "connected to ap SSID:%s", app_config.wifi_ssid);
    }
    else
    {
        ESP_LOGE(TAG, "UNEXPECTED EVENT");
    }
    // return
    return ESP_OK;
}
