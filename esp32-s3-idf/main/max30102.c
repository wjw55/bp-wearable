#include "max30102.h"

#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define MAX30102_ADDR 0x57

#define REG_INTR_STATUS_1 0x00
#define REG_INTR_STATUS_2 0x01
#define REG_INTR_ENABLE_1 0x02
#define REG_INTR_ENABLE_2 0x03
#define REG_FIFO_WR_PTR 0x04
#define REG_OVF_COUNTER 0x05
#define REG_FIFO_RD_PTR 0x06
#define REG_FIFO_DATA 0x07
#define REG_FIFO_CONFIG 0x08
#define REG_MODE_CONFIG 0x09
#define REG_SPO2_CONFIG 0x0A
#define REG_LED1_PA 0x0C
#define REG_LED2_PA 0x0D
#define REG_REV_ID 0xFE
#define REG_PART_ID 0xFF

static const char *TAG = "max30102";

static esp_err_t write_reg(max30102_t *sensor, uint8_t reg, uint8_t value)
{
    uint8_t data[2] = {reg, value};
    return i2c_master_transmit(sensor->dev, data, sizeof(data), 100);
}

static esp_err_t read_reg(max30102_t *sensor, uint8_t reg, uint8_t *value)
{
    return i2c_master_transmit_receive(sensor->dev, &reg, 1, value, 1, 100);
}

static esp_err_t read_regs(max30102_t *sensor, uint8_t reg, uint8_t *data, size_t len)
{
    return i2c_master_transmit_receive(sensor->dev, &reg, 1, data, len, 100);
}

esp_err_t max30102_init(max30102_t *sensor, gpio_num_t sda_gpio, gpio_num_t scl_gpio)
{
    ESP_RETURN_ON_FALSE(sensor != NULL, ESP_ERR_INVALID_ARG, TAG, "sensor handle is null");

    i2c_master_bus_config_t bus_config = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .i2c_port = I2C_NUM_0,
        .sda_io_num = sda_gpio,
        .scl_io_num = scl_gpio,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };

    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&bus_config, &sensor->bus), TAG, "i2c bus init failed");

    i2c_device_config_t dev_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = MAX30102_ADDR,
        .scl_speed_hz = 400000,
    };

    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(sensor->bus, &dev_config, &sensor->dev), TAG, "i2c add device failed");

    uint8_t part_id = 0;
    ESP_RETURN_ON_ERROR(read_reg(sensor, REG_PART_ID, &part_id), TAG, "part id read failed");
    const uint8_t detected_part_id = part_id;
    if (detected_part_id != 0x15) {
        ESP_LOGW(TAG, "unexpected part id 0x%02x, continuing in MAX30102-compatible mode", detected_part_id);
    }

    ESP_RETURN_ON_ERROR(write_reg(sensor, REG_MODE_CONFIG, 0x40), TAG, "sensor reset failed");
    vTaskDelay(pdMS_TO_TICKS(100));

    uint8_t status = 0;
    (void)read_reg(sensor, REG_INTR_STATUS_1, &status);
    (void)read_reg(sensor, REG_INTR_STATUS_2, &status);

    ESP_RETURN_ON_ERROR(write_reg(sensor, REG_INTR_ENABLE_1, 0x00), TAG, "interrupt disable failed");
    ESP_RETURN_ON_ERROR(write_reg(sensor, REG_INTR_ENABLE_2, 0x00), TAG, "interrupt disable failed");

    ESP_RETURN_ON_ERROR(write_reg(sensor, REG_FIFO_WR_PTR, 0x00), TAG, "fifo wr reset failed");
    ESP_RETURN_ON_ERROR(write_reg(sensor, REG_OVF_COUNTER, 0x00), TAG, "fifo ovf reset failed");
    ESP_RETURN_ON_ERROR(write_reg(sensor, REG_FIFO_RD_PTR, 0x00), TAG, "fifo rd reset failed");

    // Average 4 samples, enable FIFO rollover, almost-full threshold at 15 samples.
    ESP_RETURN_ON_ERROR(write_reg(sensor, REG_FIFO_CONFIG, (2 << 5) | (1 << 4) | 0x0F), TAG, "fifo config failed");

    // SpO2 mode: Red + IR channels.
    ESP_RETURN_ON_ERROR(write_reg(sensor, REG_MODE_CONFIG, 0x03), TAG, "mode config failed");

    // ADC range 4096 nA, sample rate 100 Hz, pulse width 411 us.
    ESP_RETURN_ON_ERROR(write_reg(sensor, REG_SPO2_CONFIG, (1 << 5) | (3 << 2) | 3), TAG, "spo2 config failed");

    ESP_RETURN_ON_ERROR(write_reg(sensor, REG_LED1_PA, 0x1F), TAG, "red led config failed");
    ESP_RETURN_ON_ERROR(write_reg(sensor, REG_LED2_PA, 0x1F), TAG, "ir led config failed");

    uint8_t rev_id = 0;
    (void)read_reg(sensor, REG_REV_ID, &rev_id);
    ESP_LOGI(TAG, "MAX30102 initialized, part=0x%02x rev=0x%02x", detected_part_id, rev_id);

    return ESP_OK;
}

esp_err_t max30102_read_sample(max30102_t *sensor, uint32_t *red, uint32_t *ir)
{
    ESP_RETURN_ON_FALSE(sensor != NULL && red != NULL && ir != NULL, ESP_ERR_INVALID_ARG, TAG, "bad arg");

    uint8_t wr_ptr = 0;
    uint8_t rd_ptr = 0;
    ESP_RETURN_ON_ERROR(read_reg(sensor, REG_FIFO_WR_PTR, &wr_ptr), TAG, "read wr ptr failed");
    ESP_RETURN_ON_ERROR(read_reg(sensor, REG_FIFO_RD_PTR, &rd_ptr), TAG, "read rd ptr failed");

    if (wr_ptr == rd_ptr) {
        return ESP_ERR_NOT_FOUND;
    }

    uint8_t fifo_data[6] = {0};
    ESP_RETURN_ON_ERROR(read_regs(sensor, REG_FIFO_DATA, fifo_data, sizeof(fifo_data)), TAG, "fifo read failed");

    *red = (((uint32_t)fifo_data[0] << 16) | ((uint32_t)fifo_data[1] << 8) | fifo_data[2]) & 0x03FFFF;
    *ir = (((uint32_t)fifo_data[3] << 16) | ((uint32_t)fifo_data[4] << 8) | fifo_data[5]) & 0x03FFFF;

    return ESP_OK;
}
