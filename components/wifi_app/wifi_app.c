#include "wifi_app.h"
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_sntp.h"
#include "esp_timer.h"

static const char *TAG_WIFI = "WIFI_APP";
static const char *TAG_SNTP = "SNTP_SYNC";

// Event Group theo dõi trạng thái kết nối Wi-Fi
static EventGroupHandle_t s_wifi_event_group;
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

static int s_retry_num = 0;
static bool is_first_boot = true;
time_t boot_epoch_time = 0;
// Hàm báo hiệu khi đồng bộ thời gian từ Internet thành công
static void time_sync_notification_cb(struct timeval *tv)
{
    time_t now;
    time(&now);
    int64_t current_uptime = esp_timer_get_time() / 1000000LL;
    boot_epoch_time = now - current_uptime; // Mốc thời gian thực lúc cắm điện!
    ESP_LOGI(TAG_SNTP, "Dong bo thoi gian thuc thanh cong!");
}

// Hàm khởi tạo SNTP lấy giờ Việt Nam
static void start_sntp_sync(void)
{
    if (esp_sntp_enabled()) {
        return;
    }

    ESP_LOGI(TAG_SNTP, "Bat dau dong bo thoi gian qua mang...");

    // Cấu hình múi giờ Việt Nam (UTC+7)
    setenv("TZ", "ICT-7", 1);
    tzset();

    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_set_time_sync_notification_cb(time_sync_notification_cb);
    esp_sntp_init();
}

// Hàm xử lý sự kiện Wi-Fi và IP chạy ngầm
static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                               int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } 
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        // Xóa ngay bit báo có mạng để các task khác biết Wi-Fi vừa mất
        xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT);

        if (is_first_boot) {
            if (s_retry_num < MAXIMUM_RETRY) {
                esp_wifi_connect();
                s_retry_num++;
                ESP_LOGW(TAG_WIFI, "Ket noi lai WiFi lan thu %d...", s_retry_num);
            } else {
                xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
                ESP_LOGE(TAG_WIFI, "Khoi dong khong co mang, chuyen sang che do chay offline!");
                is_first_boot = false;
                esp_wifi_connect();
            }
        } else {
            // Đang chạy 24/24 mà rớt mạng: tự động kết nối lại liên tục
            ESP_LOGW(TAG_WIFI, "Mat ket noi WiFi giua chung! Dang thu ket noi lai...");
            esp_wifi_connect();
        }
    } 
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG_WIFI, "Da nhan dia chi IP: " IPSTR, IP2STR(&event->ip_info.ip));

        s_retry_num = 0;
        is_first_boot = false;

        // Có IP rồi mới tiến hành đồng bộ giờ SNTP
        start_sntp_sync();

        xEventGroupClearBits(s_wifi_event_group, WIFI_FAIL_BIT);
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

// Hàm khởi tạo Wi-Fi Station
void wifi_init_sta(void)
{
    s_wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT,
        ESP_EVENT_ANY_ID,
        &wifi_event_handler,
        NULL,
        NULL)
    );
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT,
        IP_EVENT_STA_GOT_IP,
        &wifi_event_handler,
        NULL,
        NULL)
    );

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASS,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG_WIFI, "Khoi tao WiFi Station thanh cong. Dang ket noi toi: %s...", WIFI_SSID);

    EventBits_t bits = xEventGroupWaitBits(
        s_wifi_event_group,
        WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
        pdFALSE,
        pdFALSE,
        portMAX_DELAY
    );

    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG_WIFI, "Da ket noi toi SSID: %s thanh cong!", WIFI_SSID);
    } else if (bits & WIFI_FAIL_BIT) {
        ESP_LOGE(TAG_WIFI, "Khong the ket noi toi SSID: %s (se tu dong thu lai ngam)", WIFI_SSID);
    }
}

// Hàm tiện ích để server_task gọi kiểm tra trạng thái mạng bất cứ lúc nào
bool wifi_is_connected(void)
{
    if (s_wifi_event_group == NULL) {
        return false;
    }
    EventBits_t bits = xEventGroupGetBits(s_wifi_event_group);
    return (bits & WIFI_CONNECTED_BIT) != 0;
}