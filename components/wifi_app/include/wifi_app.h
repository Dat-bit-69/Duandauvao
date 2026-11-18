#ifndef WIFI_APP_H
#define WIFI_APP_H

#include <stdbool.h> // Thêm dòng này để hết báo lỗi đỏ ở chữ bool
#include <time.h>    // Thêm dòng này cho kiểu time_t
#include "esp_err.h"

// Cấu hình mạng WiFi
#define WIFI_SSID      "P. 201"
#define WIFI_PASS      "82160951"
#define MAXIMUM_RETRY  3

// Cho phép main.c truy cập mốc thời gian thực lúc cắm điện
extern time_t boot_epoch_time;

void wifi_init_sta(void);
bool wifi_is_connected(void);

#endif // WIFI_APP_H