#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "freertos/queue.h"
#include "esp_system.h"
#include "bh1750.h"                       // Thư viện BH1750
#include "nvs_flash.h"
#include "wifi_app.h"                     // Thư viện WiFi
#include "esp_http_client.h"
#include "esp_timer.h"
#include <time.h>
#include <sys/time.h>
#include "esp_littlefs.h"
#include <sys/stat.h>
#include <unistd.h>

#define SERVER_URL "http://192.168.103.104:5000/api/sensor"
#define OFFLINE_FILE_PATH "/littlefs/offline_data.bin"

static const char *TAG = "BH1750";
static const char *TAG_SERVER = "SERVER";

typedef struct {
    float lux;              // Giá trị ánh sáng (hoặc -1.0f, -2.0f nếu lỗi)
    char timestamp[24];     // Chuỗi thời gian dạng "2026-11-18 19:10:00"
    bool is_time_valid;     // Cờ báo giờ đã đồng bộ SNTP chuẩn hay chưa
    int64_t uptime_sec;     // Số giây tính từ lúc ESP32 được cắm điện
} sensor_data_t;

static QueueHandle_t lux_queue = NULL;

static bool get_current_time_str(char *buffer, size_t max_len)
{
    time_t now;
    struct tm timeinfo;

    // Đọc số giây hiện tại từ bộ đếm RTC bên trong chip ESP32 (mất < 1 micro-giây)
    time(&now);
    // Đổi số giây đó sang ngày, tháng, năm, giờ, phút, giây theo múi giờ đã cài
    localtime_r(&now, &timeinfo);

    // Format thành chuỗi chuẩn MySQL: "2026-11-18 19:13:00"
    strftime(buffer, max_len, "%Y-%m-%d %H:%M:%S", &timeinfo);

    // Kiểm tra xem năm đã lớn hơn 2026 chưa. 
    // Nếu chưa sync SNTP lần nào, chip sẽ đếm từ năm 1970 -> trả về false (giờ thô)
    if (timeinfo.tm_year < (2026 - 1900)) {
        return false;
    }
    return true;
}

/**
 * @brief Sensor task
 */
void sensor_task(void *pvParameters)
{
    vTaskDelay(pdMS_TO_TICKS(180)); // Đợi BH1750 khởi động xong
    // Đảm bảo múi giờ Việt Nam luôn đúng kể cả khi chưa kịp kết nối Wi-Fi
    setenv("TZ", "ICT-7", 1);
    tzset();

    float lux = 0.0f;
    sensor_data_t packet; //  chứa cả lux + thời gian

    // Lấy mốc nhịp ban đầu của FreeRTOS để đếm chu kỳ chính xác
    TickType_t xLastWakeTime = xTaskGetTickCount();
    const TickType_t xFrequency = pdMS_TO_TICKS(10000); // Chu kỳ chuẩn 10.000ms (10 giây)

    while (1) {
        // 1. Đọc cảm biến BH1750
        esp_err_t err = bh1750_read_lux(&lux);
        if (err == ESP_OK) {
            packet.lux = lux;
            ESP_LOGI(TAG, "Cuong do anh sang: %.2f Lux", lux);
        } else {
            packet.lux = -1.0f; // Mã lỗi đọc cảm biến
            ESP_LOGW(TAG, "Doc du lieu tu BH1750 that bai. Ma loi: %s", esp_err_to_name(err));
        }

        // 2. Chốt ngay mốc thời gian tại đúng khoảnh khắc vừa đo xong
        packet.is_time_valid = get_current_time_str(packet.timestamp, sizeof(packet.timestamp));
        // esp_timer_get_time() trả về micro-giây từ lúc boot -> chia 1.000.000 để ra giây
        packet.uptime_sec = esp_timer_get_time() / 1000000LL;
        ESP_LOGI(TAG, "Thoi gian do: %s (%s)", 
                 packet.timestamp, // thời gian từ lúc đo
                 packet.is_time_valid ? "Gio chuan" : "Gio tho chua sync");

        // 3. Đẩy cả gói tin (lux + thời gian) vào Queue
       xQueueSend(lux_queue, &packet, 0);

        // 4. Ngủ đúng phần thời gian còn lại để tròn 10 giây (không bị trôi nhịp)
        vTaskDelayUntil(&xLastWakeTime, xFrequency);
    }
}

// Khởi tạo hệ thống tệp LittleFS trên phân vùng "storage"
static esp_err_t init_littlefs(void)
{
    esp_vfs_littlefs_conf_t conf = {
        .base_path = "/littlefs",
        .partition_label = "storage",
        .format_if_mount_failed = true,
        .dont_mount = false,
    };

    esp_err_t ret = esp_vfs_littlefs_register(&conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG_SERVER, "Khoi tao LittleFS that bai (%s)", esp_err_to_name(ret));
        return ret;
    }
    size_t total = 0, used = 0;
    esp_littlefs_info(conf.partition_label, &total, &used);
    ESP_LOGI(TAG_SERVER, "LittleFS san sang! Dung luong: %d / %d bytes", used, total);

    if (unlink(OFFLINE_FILE_PATH) == 0) {
        ESP_LOGW(TAG_SERVER, "Da don sach file offline cu tu lan khoi dong truoc!");
    }

    return ESP_OK;
}

// Ghi nối thêm 1 gói tin vào cuối file Flash khi mất mạng
static void save_packet_to_flash(const sensor_data_t *data)
{
    FILE *f = fopen(OFFLINE_FILE_PATH, "ab");
    if (f == NULL) {
        ESP_LOGE(TAG_SERVER, "Khong mo duoc file Flash de luu offline!");
        return;
    }
    fwrite(data, sizeof(sensor_data_t), 1, f);
    fclose(f);
    ESP_LOGW(TAG_SERVER, "Da luu goi tin vao Flash (uptime: %lld s)", data->uptime_sec);
}

// Chốt chuỗi thời gian cuối cùng cho gói tin trước khi gửi lên Server
static void resolve_final_timestamp(const sensor_data_t *data, char *out_json_ts, size_t max_len)
{
    if (data->is_time_valid) {
        // Tình huống 1: Lúc đo đã có giờ SNTP chuẩn 
        snprintf(out_json_ts, max_len, "\"%s\"", data->timestamp);
    } 
    else if (boot_epoch_time > 0) {
        // Tình huống 2: Lúc đo chưa có mạng vì chưa kịp đồng bộ, hoặc mất mạng giữa chừng 
        time_t recovered_time = boot_epoch_time + (time_t)data->uptime_sec;
        struct tm timeinfo;
        char time_buf[24];

        localtime_r(&recovered_time, &timeinfo);
        strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", &timeinfo);
        snprintf(out_json_ts, max_len, "\"%s\"", time_buf);
    } 
    else {
        // Tình huống 3: Từ lúc cắm điện vẫn chưa từng có Internet -> để null
        snprintf(out_json_ts, max_len, "null");
    }
}


/**
 * @brief Server task
 */
// Đóng gói JSON và gửi từng gói tin lên server. Trả về true nếu thành công.
static bool send_packet_to_server(esp_http_client_handle_t client, const sensor_data_t *data)
{
    char payload[160];
    char ts_json[32];

    resolve_final_timestamp(data, ts_json, sizeof(ts_json));

    if (data->lux >= 0.0f) {
        snprintf(payload, sizeof(payload),
                 "{\"lux\": %.2f, \"status\": \"OK\", \"timestamp\": %s}",
                 data->lux, ts_json);
    } else if (data->lux == -1.0f) {
        snprintf(payload, sizeof(payload),
                 "{\"lux\": null, \"status\": \"READ_ERROR\", \"timestamp\": %s}",
                 ts_json);
    } else {
        snprintf(payload, sizeof(payload),
                 "{\"lux\": null, \"status\": \"HARDWARE_ERROR\", \"timestamp\": %s}",
                 ts_json);
    }

    ESP_LOGI(TAG_SERVER, "Dang gui: %s", payload);

    esp_http_client_set_url(client, SERVER_URL);
    esp_http_client_set_method(client, HTTP_METHOD_POST);
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, payload, strlen(payload));

    esp_err_t err = esp_http_client_perform(client);
    int status_code = esp_http_client_get_status_code(client);
    esp_http_client_close(client);

    if (err == ESP_OK && status_code >= 200 && status_code < 300) {
        ESP_LOGI(TAG_SERVER, "Gui thanh cong! (HTTP %d)", status_code);
        return true;
    }

    ESP_LOGE(TAG_SERVER, "Gui that bai! Loi: %s, HTTP: %d", esp_err_to_name(err), status_code);
    return false;
}

// Xả toàn bộ dữ liệu tồn đọng trong Flash lên server khi có mạng lại
static void flush_offline_data(esp_http_client_handle_t client)
{
    struct stat st;
    if (stat(OFFLINE_FILE_PATH, &st) != 0) {
        return;
    }
    FILE *f = fopen(OFFLINE_FILE_PATH, "rb");
    if (f == NULL) {
        return;
    }
    ESP_LOGI(TAG_SERVER, "Phat hien du lieu offline (%ld bytes). Bat dau gui bu...", st.st_size);
    sensor_data_t old_data;
    bool all_sent = true;
    while (fread(&old_data, sizeof(sensor_data_t), 1, f) == 1) {
        if (!wifi_is_connected() || !send_packet_to_server(client, &old_data)) {
            all_sent = false;
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    fclose(f);
    if (all_sent) {
        unlink(OFFLINE_FILE_PATH);
        ESP_LOGI(TAG_SERVER, "Da gui bu xong toan bo du lieu offline va xoa file tam!");
    }
}

void server_task(void *pvParameters)
{
    sensor_data_t data;

    esp_http_client_config_t config = {
        .url = SERVER_URL,
        .method = HTTP_METHOD_POST,
        .timeout_ms = 5000,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);

    while (1) {
        if (xQueueReceive(lux_queue, &data, portMAX_DELAY) == pdTRUE) {
            if (!wifi_is_connected()) {
                save_packet_to_flash(&data);
                continue;
            }

            if (send_packet_to_server(client, &data)) {
                flush_offline_data(client);
            } else {
                save_packet_to_flash(&data);
            }
        }
    }
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

    //khởi tạo tệp hệ thống LittleFS trên phân vùng "storage"
    ESP_ERROR_CHECK(init_littlefs());

    // Kết nối WiFi
    wifi_init_sta();



    // 2.Khởi tạo I2C và BH1750
    lux_queue = xQueueCreate(5, sizeof(sensor_data_t)); //tạo hàng đợi để lưu giá trị lux
     
    i2c_master_bus_handle_t bus_handle;
    
    xTaskCreatePinnedToCore(server_task, "server_task", 4096, NULL, 5, NULL, 1); //tạo server task chạy trên core 1

    // logi:info, logw: warning, loge: error
    
    sensor_data_t errhw = {  
        .lux = -2.0f,
        .is_time_valid = false,
        .uptime_sec = esp_timer_get_time() / 1000000LL
    };
    errhw.is_time_valid = get_current_time_str(errhw.timestamp, sizeof(errhw.timestamp));  

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

    xTaskCreatePinnedToCore(sensor_task, "sensor_task", 6144, NULL, 5, NULL, 0);   // tạo sensor task chạy trên core 0
    
    vTaskDelete(NULL);  //xóa task main sau khi set up
}


