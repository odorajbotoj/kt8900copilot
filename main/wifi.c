#include "wifi.h"

#define TAG "WIFI_CONN"

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT BIT1

static EventGroupHandle_t wifi_event_group;
static int retry_num = 0;

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
            if (retry_num < WIFI_MAX_RETRY)
            {
                esp_wifi_connect();
                ++retry_num;
                ESP_LOGI(TAG, "retry to connect to the AP");
            }
            else
            {
                xEventGroupSetBits(wifi_event_group, WIFI_FAIL_BIT);
            }
        }
    }
    else if (ev_base == IP_EVENT)
    {
        if (ev_id == IP_EVENT_STA_GOT_IP)
        {
            ip_event_got_ip_t *ev = (ip_event_got_ip_t *)ev_data;
            ESP_LOGI(TAG, "got ip:" IPSTR, IP2STR(&ev->ip_info.ip));
            retry_num = 0;
            xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
        }
    }
}

esp_err_t nvs_init(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ERR_CHK(nvs_flash_erase(), "Failed to erase NVS Flash.");
        ERR_CHK(nvs_flash_init(), "Failed to init NVS Flash.");
    }
    return ESP_OK;
}

esp_err_t wifi_init()
{
    // init
    wifi_event_group = xEventGroupCreate();
    ERR_CHK(esp_netif_init(), "Failed to init NETIF.");
    ERR_CHK(esp_event_loop_create_default(), "Failed to create EventLoop.");
    esp_netif_create_default_wifi_sta();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ERR_CHK(esp_wifi_init(&cfg), "Failed to init WiFi.");
    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ERR_CHK(esp_event_handler_instance_register(WIFI_EVENT,
                                                ESP_EVENT_ANY_ID,
                                                &event_handler,
                                                NULL,
                                                &instance_any_id),
            "Failed to register WiFi event callback.");
    ERR_CHK(esp_event_handler_instance_register(IP_EVENT,
                                                IP_EVENT_STA_GOT_IP,
                                                &event_handler,
                                                NULL,
                                                &instance_got_ip),
            "Failed to register WiFi event callback.");
    // set ssid and password
    wifi_config_t wifi_config = {0};
    memcpy(wifi_config.sta.ssid, app_config.wifi_ssid, sizeof(wifi_config.sta.ssid));
    memcpy(wifi_config.sta.password, app_config.wifi_password, sizeof(wifi_config.sta.password));
    ERR_CHK(esp_wifi_set_mode(WIFI_MODE_STA), "Failed to set WiFi mode.");
    ERR_CHK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config), "Failed to set WiFi config.");
    // start
    ERR_CHK(esp_wifi_start(), "Failed to start WiFi.");
    ESP_LOGI(TAG, "wifi_init_sta finished.");
    // wait for result
    EventBits_t bits = xEventGroupWaitBits(wifi_event_group,
                                           WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                           pdFALSE,
                                           pdFALSE,
                                           portMAX_DELAY);
    if (bits & WIFI_CONNECTED_BIT)
    {
        ESP_LOGI(TAG, "connected to ap SSID:%s", app_config.wifi_ssid);
    }
    else if (bits & WIFI_FAIL_BIT)
    {
        ESP_LOGI(TAG, "Failed to connect to WiFi");
    }
    else
    {
        ESP_LOGE(TAG, "UNEXPECTED EVENT");
    }
    // return
    return ESP_OK;
}
