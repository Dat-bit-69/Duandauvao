#ifndef WIFI_APP_H
#define WIFI_APP_H

#include "esp_err.h"

// Cấu hình mạng WiFi
#define WIFI_SSID       "TP-Link_D8AA"
#define WIFI_PASS       "82160951"
#define MAXIMUM_RETRY   3

/**
 * @brief Khởi tạo Station mode và kết nối vào WiFi
 */
void wifi_init_sta(void);

#endif  // WIFI_APP_H