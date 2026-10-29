#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "freertos/queue.h"
#include "esp_system.h"
#include "bh1750.h"                       // Thư viện BH1750
#include "esp_event.h"
#include "nvs_flash.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "wifi_app.h"                     // Thư viện WiFi
#include "esp_http_client.h"

#define SERVER_URL "http://192.168.103.104:5000/api/sensor"

static const char *TAG = "BH1750";
static const char *TAG_SERVER = "SERVER";

static QueueHandle_t lux_queue = NULL;

/**
 * @brief Sensor task
 */
void sensor_task(void *pvParameters)
{
    vTaskDelay(pdMS_TO_TICKS(180));  //đợi max 180ms để vào chế độ đo
    float lux = 0.0f;
    float error_value = -1.0f; // Giá trị lỗi
    float value;
    while (1) {
        esp_err_t err = bh1750_read_lux(&lux);
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "Cuong do anh sang: %.2f Lux", lux);
            if (lux_queue != NULL) {
                value = lux;  
            }
        } else {
            ESP_LOGW(TAG, "Doc du lieu tu BH1750 that bai. Ma loi: %s", esp_err_to_name(err));
            float error_value = -1.0f; // Giá trị lỗi
            if (lux_queue != NULL) {
                value = error_value;  
            }
        }
        xQueueSend(lux_queue, &value, 0);
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}

/**
 * @brief Server task
 */
void server_task(void *pvParameters){
    float data;
    char payload[128];  //khai báo chuỗi chứa payload 

    //cấu hình http
    esp_http_client_config_t config = {   
        .url = SERVER_URL,                            //gắn nguyên url
        .method = HTTP_METHOD_POST,                   //phương thức post
        .timeout_ms = 5000,                            
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);


    while (1) {
        if (xQueueReceive(lux_queue, &data, portMAX_DELAY) == pdTRUE) {  //chờ vô hạn
            if (data >= 0.0f) {
                snprintf(payload, sizeof(payload), "{\"lux\": %.2f, \"status\": \"OK\"}", data);   //ép về json
            } else if (data == -1.0f) {
                snprintf(payload, sizeof(payload), "{\"lux\": null, \"status\": \"READ_ERROR\"}");
            } else if (data == -2.0f) {
                snprintf(payload, sizeof(payload), "{\"lux\": null, \"status\": \"HARDWARE_ERROR\"}");
            }
            ESP_LOGI(TAG_SERVER, "Goi tin: %s", payload);  //in ra terminal chuỗi json
        
            esp_http_client_set_url(client, SERVER_URL);
            esp_http_client_set_method(client, HTTP_METHOD_POST);
            esp_http_client_set_header(client, "Content-Type", "application/json");
            esp_http_client_set_post_field(client, payload, strlen(payload));

            //  Gửi HTTP request
            esp_err_t err = esp_http_client_perform(client);
            if (err == ESP_OK) {
                int status_code = esp_http_client_get_status_code(client);
                ESP_LOGI(TAG_SERVER, "Gui thanh cong! Ma phan hoi (Status): %d", status_code);
            } else {
                ESP_LOGE(TAG_SERVER, "Gui that bai! Ma loi: %s", esp_err_to_name(err));
            }
        }
    }

    // Giải phóng tài nguyên client nếu thoát vòng lặp
    esp_http_client_cleanup(client);
    vTaskDelete(NULL);
}

/**
 * @brief app_main
 */
void app_main(void)
{
    // 1.Kết nối Wifi
    // Bắt buộc: Khởi tạo NVS Flash trước khi dùng WiFi
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Kết nối WiFi
    wifi_init_sta();



    // 2.Khởi tạo I2C và BH1750
    lux_queue = xQueueCreate(5, sizeof(float)); //tạo hàng đợi để lưu giá trị lux
    
    i2c_master_bus_handle_t bus_handle;
    
    xTaskCreatePinnedToCore(server_task, "server_task", 4096, NULL, 5, NULL, 1); //tạo server task chạy trên core 1

    // logi:info, logw: warning, loge: error
    // Khởi tạo I2C bus
    float errhw = -2.0f;    //mã lỗi phần cứng
    esp_err_t err = khoitao_i2c(&bus_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Khoi tao I2C that bai. Dung task doc cam bien.");
        ESP_LOGW(TAG, "Ma loi: %s", esp_err_to_name(err));   //in ra mã lỗi
        if (lux_queue != NULL) {
            xQueueSend(lux_queue, &errhw, 0);
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
        vTaskDelete(NULL);
    } else {
        ESP_LOGI(TAG, "Khoi tao I2C thanh cong.");
    }

    // Khởi tạo BH1750
    err = khoitao_bh1750(bus_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Khoi tao BH1750 that bai. Dung task doc cam bien.");
        ESP_LOGW(TAG, "Ma loi: %s", esp_err_to_name(err));  //in ra mã lỗi
        if (lux_queue != NULL) {
            xQueueSend(lux_queue, &errhw, 0);
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
        vTaskDelete(NULL);
    } else {
        ESP_LOGI(TAG, "Khoi tao BH1750 thanh cong.");
    }

    ESP_LOGI(TAG, "Phan cung san sang. Tao sensor_task...");        //vào sensor task

    xTaskCreatePinnedToCore(sensor_task, "sensor_task", 4096, NULL, 5, NULL, 0);   // tạo sensor task chạy trên core 0
    
    vTaskDelete(NULL);  //xóa task main sau khi set up
}


