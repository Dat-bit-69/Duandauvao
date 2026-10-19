#include "wifi_app.h"
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"

#define WIFI_SSID       "TP-Link_D8AA"
#define WIFI_PASS       "82160951"
#define MAXIMUM_RETRY   3

static const char *TAG_WIFI = "WIFI_APP";

// Khởi tạo Event Group để theo dõi trạng thái kết nối
static EventGroupHandle_t s_wifi_event_group;
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

static int s_retry_num = 0;

// Hàm xử lý sự kiện WiFi và IP  --- Chạy ngầm tách khỏi main
static void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data)  
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    }
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry_num < MAXIMUM_RETRY) {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGW(TAG_WIFI, "Ket noi lai WiFi lan thu %d...", s_retry_num);
        } else {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);      // bit sai
        }
        ESP_LOGE(TAG_WIFI, "Ket noi WiFi that bai");
    } 
    if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG_WIFI, "Da nhan dia chi IP: " IPSTR, IP2STR(&event->ip_info.ip));   //đưa ip sang dạng . . . đọc được
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);     // bit đúng
    }
}

// Hàm khởi tạo và kết nối WiFi
void wifi_init_sta(void)
{
    s_wifi_event_group = xEventGroupCreate();

    // 1. Khởi tạo môi trường kết nối wifi
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());              //Gọi hàm tạo vòng lặp sự kiện mặc định xử lý Wifi, IP
    esp_netif_create_default_wifi_sta();                           //Station

    // 2. Khởi tạo 
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();           //Khai báo (mặc định của nxs thiết lập)
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));  //*đc cấp phát nhiều bộ nhớ

    // Đăng ký nhận sự kiện WiFi và nhận IP
    ESP_ERROR_CHECK(esp_event_handler_instance_register(           //Hàm đăng ký nhận sk Wifi
        WIFI_EVENT,
        ESP_EVENT_ANY_ID,
        &wifi_event_handler,                                                
        NULL,
        NULL)
    );
    ESP_ERROR_CHECK(esp_event_handler_instance_register(           //Hàm đăng ký nhận sk IP
        IP_EVENT,
        IP_EVENT_STA_GOT_IP,
        &wifi_event_handler,
        NULL,
        NULL)
    );

    // 3. Cấu hình SSID và Password
    wifi_config_t wifi_config = {.sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASS,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,   // chuẩn bảo mật WPA2
        },
    };

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG_WIFI, "Khoi tao WiFi Station thanh cong. Dang ket noi toi: %s...", WIFI_SSID);

    // 4. Chờ kết quả set bits trả về
    // Hàm if không thì chạy quá nhanh, hàm while thì ngốn tài nguyên CPU, nên dùng EGWB để chờ kết quả hiệu quả
    EventBits_t bits = xEventGroupWaitBits(
        s_wifi_event_group,                     //event group cần theo dõi
        WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,     //các bit cần chờ
        pdFALSE,                                //xóa các bit đã được set sau khi chờ
        pdFALSE,                                //không chờ tất cả các bit
        portMAX_DELAY                           //thời gian chờ tối đa
    );

    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG_WIFI, "Da ket noi toi SSID: %s thanh cong!", WIFI_SSID);
    } else if (bits & WIFI_FAIL_BIT) {
        ESP_LOGE(TAG_WIFI, "Khong the ket noi toi SSID: %s", WIFI_SSID);
    }
}