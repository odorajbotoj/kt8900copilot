#include "config.h"

#define TAG "CONFIG"

app_config_t app_config = {0};
char device_mac_address[24] = {0};

void get_mac(void)
{
    uint8_t mac[8];
    esp_efuse_mac_get_default(mac);
    size_t l = esp_mac_addr_len_get(ESP_MAC_IEEE802154);
    if (l == 8)
    {
        sprintf(device_mac_address,
                "%02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X",
                mac[0],
                mac[1],
                mac[2],
                mac[3],
                mac[4],
                mac[5],
                mac[6],
                mac[7]);
    }
    else if (l == 0)
    {
        sprintf(device_mac_address,
                "%02X:%02X:%02X:%02X:%02X:%02X",
                mac[0],
                mac[1],
                mac[2],
                mac[3],
                mac[4],
                mac[5]);
    }
}

esp_err_t load_config(void)
{
    // file not exist
    if (access(MOUNT_POINT "/conf.txt", F_OK) == -1)
    {
        FILE *mac_file = NULL;
        ESP_RETURN_ON_FALSE(mac_file = fopen(MOUNT_POINT "/mac.txt", "w"), ESP_FAIL, TAG, "cannot write MAC to file.");
        fputs(device_mac_address, mac_file);
        fclose(mac_file);
        FILE *conf_file = NULL;
        ESP_RETURN_ON_FALSE(conf_file = fopen(MOUNT_POINT "/conf.txt", "w"), ESP_FAIL, TAG, "cannot write config to file.");
        fputs("wifi_ssid\nwifi_password\nws_server\ntimezone CST-8\nntp_server ntp.ntsc.ac.cn\nadc_offset -2300\ntx_limit_ms 120000", conf_file);
        fclose(conf_file);
        ESP_LOGW(TAG, "config file generated.");
        return ESP_FAIL;
    }
    // read config
    FILE *fptr = NULL;
    ESP_RETURN_ON_FALSE(fptr = fopen(MOUNT_POINT "/conf.txt", "r"), ESP_FAIL, TAG, "cannot read config file.");
    char readbuf[256] = {0};
    char keybuf[128] = {0};
    char valbuf[128] = {0};
    int split_index = 0;
    while (NULL != fgets(readbuf, sizeof(readbuf), fptr))
    {
        remove_right_space(readbuf);
        // parse
        for (int i = 0; i < 256; ++i)
        {
            if (readbuf[i] == '\0')
            {
                break;
            }
            else if (readbuf[i] == ' ')
            {
                readbuf[i] = '\0';
                split_index = i;
                break;
            }
        }
        strncpy(keybuf, readbuf, 128);
        strncpy(valbuf, readbuf + split_index + 1, 128);
        // save
        if (0 == strcmp("wifi_ssid", keybuf))
        {
            strncpy(app_config.wifi_ssid, valbuf, sizeof(app_config.wifi_ssid));
        }
        else if (0 == strcmp("wifi_password", keybuf))
        {
            strncpy(app_config.wifi_password, valbuf, sizeof(app_config.wifi_password));
        }
        else if (0 == strcmp("ws_server", keybuf))
        {
            strncpy(app_config.ws_server, valbuf, sizeof(app_config.ws_server));
        }
        else if (0 == strcmp("timezone", keybuf))
        {
            strncpy(app_config.timezone, valbuf, sizeof(app_config.timezone));
        }
        else if (0 == strcmp("ntp_server", keybuf))
        {
            strncpy(app_config.ntp_server, valbuf, sizeof(app_config.ntp_server));
        }
        else if (0 == strcmp("adc_offset", keybuf))
        {
            app_config.adc_offset = atoi(valbuf);
        }
        else if (0 == strcmp("tx_limit_ms", keybuf))
        {
            app_config.tx_limit_ms = atoi(valbuf);
        }
        // clear
        memset(readbuf, 0, sizeof(readbuf));
        memset(keybuf, 0, sizeof(keybuf));
        memset(valbuf, 0, sizeof(valbuf));
        split_index = 0;
    }
    fclose(fptr);
    return ESP_OK;
}

void print_config(void)
{
    ESP_LOGI(TAG,
             "read config:\nwifi ssid: %s\nwifi password length: %zu\nws server: %s\n"
             "timezone: %s\nntp server: %s\nadc offset: %d\ntx limit ms: %d",
             app_config.wifi_ssid,
             strlen(app_config.wifi_password),
             app_config.ws_server,
             app_config.timezone,
             app_config.ntp_server,
             app_config.adc_offset,
             app_config.tx_limit_ms);
}
