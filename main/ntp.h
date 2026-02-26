#pragma once

#include "esp_log.h"

#include "config.h"

#include "esp_sntp.h"
#include "esp_netif_sntp.h"

void ntp_init(void);
