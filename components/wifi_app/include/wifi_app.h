#ifndef WIFI_APP_H
#define WIFI_APP_H

#include "esp_err.h"

// Cấu hình mạng WiFi
#define WIFI_SSID       "P. 201"
#define WIFI_PASS       "82160951"
#define MAXIMUM_RETRY   3

void wifi_init_sta(void);

#endif  // WIFI_APP_H