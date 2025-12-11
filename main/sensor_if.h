#pragma once

#include "i2c_bus.h"
#include "esp_err.h"

typedef enum {
    SENSOR_TYPE_NONE = 0,
    SENSOR_TYPE_BME280,
    SENSOR_TYPE_AHT20_BMP280,
    SENSOR_TYPE_SHT41,
    SENSOR_TYPE_SHT41_BMP280,
} sensor_type_t;

// Initialize selected sensor stack (either BME280 or AHT20+BMP280)
esp_err_t sensor_init(i2c_bus_handle_t i2c_bus);

// Return detected sensor type (after sensor_init)
sensor_type_t sensor_get_type(void);

// Wake sensor(s) and trigger measurement (if required)
esp_err_t sensor_wake_and_measure(void);

// Read last measured temperature in degrees Celsius
esp_err_t sensor_read_temperature(float *out_c);

// Read last measured humidity in % (0-100). If sensor doesn't provide, return ESP_ERR_NOT_SUPPORTED
esp_err_t sensor_read_humidity(float *out_percent);

// Read last measured pressure in hPa. If sensor doesn't provide, return ESP_ERR_NOT_SUPPORTED
esp_err_t sensor_read_pressure(float *out_hpa);
