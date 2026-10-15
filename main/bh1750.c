#include "bh1750.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static i2c_master_dev_handle_t bh1750_handle = NULL;

/**
 * @brief Khởi tạo I2C bus
 */
esp_err_t khoitao_i2c(i2c_master_bus_handle_t *bus_handle)
{
    //  Cấu hình I2C bus
    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = I2C_PORT_NUM,
        .sda_io_num = I2C_SDA_IO,
        .scl_io_num = I2C_SCL_IO,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    return i2c_new_master_bus(&bus_cfg, bus_handle);
}

/**
 * @brief Thêm  BH1750 vào bus và bật chế độ đo
 */
esp_err_t khoitao_bh1750(i2c_master_bus_handle_t bus_handle)
{
    // 1. Thêm thiết bị BH1750 vào bus
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = DIA_CHI_BH1750,
        .scl_speed_hz = I2C_FREQ_HZ,
    };
    esp_err_t err = i2c_master_bus_add_device(bus_handle, &dev_cfg, &bh1750_handle);
    if (err != ESP_OK) {
        return err;
    }

    // 2. Bật nguồn và gửi lệnh chọn chế độ đo
    uint8_t lenh = BH1750_POWERON;
    err = i2c_master_transmit(bh1750_handle, &lenh, 1, pdMS_TO_TICKS(1000));
    if (err != ESP_OK) {
        return err;
    }

    vTaskDelay(pdMS_TO_TICKS(10));

    lenh = BH1750_CONT_H_RES;
    return i2c_master_transmit(bh1750_handle, &lenh, 1, pdMS_TO_TICKS(1000));
}

/**
 * @brief Đọc 2 byte dữ liệu thô và tính giá trị Lux
 */
esp_err_t bh1750_read_lux(float *lux)
{
    uint8_t data[2] = {0};

    esp_err_t err = i2c_master_receive(bh1750_handle, data, sizeof(data), pdMS_TO_TICKS(1000));
    if (err != ESP_OK) {
        return err;
    }

    uint16_t raw_data = ((uint16_t)data[0] << 8) | data[1];
    *lux = (float)raw_data / 1.2f;
    return ESP_OK;
}