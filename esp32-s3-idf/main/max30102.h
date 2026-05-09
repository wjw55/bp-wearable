#pragma once

#include <stdint.h>

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_err.h"

typedef struct {
    i2c_master_bus_handle_t bus;
    i2c_master_dev_handle_t dev;
} max30102_t;

esp_err_t max30102_init(max30102_t *sensor, gpio_num_t sda_gpio, gpio_num_t scl_gpio);
esp_err_t max30102_read_sample(max30102_t *sensor, uint32_t *red, uint32_t *ir);
