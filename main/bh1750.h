#ifndef BH1750_H
#define BH1750_H

#include "esp_err.h"
#include "driver/i2c_master.h"

// Cấu hình I2C và chân kết nối
#define I2C_SCL_IO              GPIO_NUM_22      
#define I2C_SDA_IO              GPIO_NUM_21
#define I2C_PORT_NUM            I2C_NUM_0
#define I2C_FREQ_HZ             100000

// Cấu hình BH1750
#define DIA_CHI_BH1750          0x23 

// Mã lệnh
#define BH1750_POWERON          0x01
#define BH1750_CONT_H_RES       0x10

// Khai báo các hàm điều khiển BH1750
esp_err_t khoitao_i2c(i2c_master_bus_handle_t *bus_handle);
esp_err_t khoitao_bh1750(i2c_master_bus_handle_t bus_handle);
esp_err_t bh1750_read_lux(float *lux);

#endif // BH1750_H