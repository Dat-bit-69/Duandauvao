

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "driver/i2c_master.h"

static const char *TAG = "BH1750";

//cấu hình i2c
#define I2C_SCL_IO              GPIO_NUM_22      
#define I2C_SDA_IO              GPIO_NUM_21
#define I2C_PORT_NUM            I2C_NUM_0
#define I2C_FREQ_HZ             100000         

//cấu hình bh1750

#define DIA_CHI_BH1750          0x23 

//mã lệnh
#define BH1750_POWERON          0x01
#define BH1750_CONT_H_RES       0x10            

static i2c_master_dev_handle_t bh1750_handle  = NULL;


/**
 * @brief Khởi tạo I2C Bus và BH1750
 */
static esp_err_t khoitao_i2c_bh1750(void)
{
    // 1. Cấu hình I2C bus
    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = I2C_PORT_NUM,
        .sda_io_num = I2C_SDA_IO,
        .scl_io_num = I2C_SCL_IO,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    i2c_master_bus_handle_t bus_handle;
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_cfg, &bus_handle));
    //esp_err_t err = i2c_new_master_bus(&bus_cfg, &bus_handle);
    //if (err != ESP_OK) {
    //printf("Khoi tao I2C that bai!");
    //abort();
    //}

    // 2. Thêm thiết bị BH1750 vào bus
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = DIA_CHI_BH1750,
        .scl_speed_hz = I2C_FREQ_HZ,
    };
     return i2c_master_bus_add_device(bus_handle, &dev_cfg, &bh1750_handle);
}

/**
 * @brief Bật nguồn và gửi lệnh chọn chế độ đo
 */
static esp_err_t bh1750_start_mode(uint8_t mode) // int 4 byte, uint8_t 1 byte, uint16_t 2 byte, uint32_t 4 byte
{
    uint8_t lenh = BH1750_POWERON; //bật nguồn bh1750
    
    esp_err_t ret = i2c_master_transmit(bh1750_handle, &lenh, 1, pdMS_TO_TICKS(1000)); //hàm truyền dữ liệu sang bh1750
    if (ret != ESP_OK) return ret;

    vTaskDelay(pdMS_TO_TICKS(10));

    //gửi lệnh chọn chế độ đo
    lenh = mode;
    return i2c_master_transmit(bh1750_handle, &lenh, 1, pdMS_TO_TICKS(1000));
}

/**
 * @brief Đọc 2 byte dữ liệu thô và tính giá trị Lux
 */
static esp_err_t bh1750_read_lux(float *lux)
{
    uint8_t data[2] = {0};  //mảng chứa 2 byte thô từ cảm biến

    esp_err_t ret = i2c_master_receive(bh1750_handle, data, sizeof(data), pdMS_TO_TICKS(1000)); //nhận 2 byte 
    if (ret != ESP_OK) {
        return ret;
    }

    //
    uint16_t raw_data = ((uint16_t)data[0] << 8) | data[1];//chuyển high byte sang 16 bit rồi dịch sang 8, ghép với low byte

   
    *lux = (float)raw_data / 1.2f;

    return ESP_OK;
}

void app_main(void)
{
    // logi:info, logw: warning, loge: error
    ESP_LOGI(TAG, "Khoi tao I2C cho BH1750...");
    if (khoitao_i2c_bh1750() != ESP_OK) {
        ESP_LOGE(TAG, "Loi khoi tao I2C");
        return;
    }

    // Cấu hình chế độ Continuously H-Resolution
    if (bh1750_start_mode(BH1750_CONT_H_RES) != ESP_OK) {
        ESP_LOGE(TAG, "Khong the gui lenh toi BH1750. Kiem tra lai day noi hoac ADDR!");
        return;
    }


    vTaskDelay(pdMS_TO_TICKS(180));  //đợi max 180ms để vào chế độ đo

    float lux = 0.0f;
    while (1) {
        if (bh1750_read_lux(&lux) == ESP_OK) {
            ESP_LOGI(TAG, "Cuong do anh sang: %.2f Lux", lux);
        } else {
            ESP_LOGW(TAG, "Loi doc du lieu tu BH1750!");
        }

       
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}