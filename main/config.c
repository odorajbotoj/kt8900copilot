#include "config.h"

#define TAG "CONFIG"

app_config_t app_config = {0};

esp_err_t load_config()
{
    // read config
    FILE *fptr = NULL;
    fptr = fopen(MOUNT_POINT "/conf.txt", "r");
    if (fptr == NULL)
    {
        ESP_LOGE(TAG, "cannot read config file.");
        return ESP_ERR_NOT_FOUND;
    }
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
            strcpy(app_config.wifi_ssid, valbuf);
        }
        else if (0 == strcmp("wifi_password", keybuf))
        {
            strcpy(app_config.wifi_password, valbuf);
        }
        else if (0 == strcmp("server_addr", keybuf))
        {
            strcpy(app_config.server_addr, valbuf);
        }
        else if (0 == strcmp("adc_offset", keybuf))
        {
            app_config.adc_offset = atoi(valbuf);
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
