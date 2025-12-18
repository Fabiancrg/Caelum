/*
 * VEML7700 Ambient Light Sensor Driver
 * High-accuracy ambient light sensor with I2C interface
 */

#pragma once

#include "i2c_bus.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* VEML7700 I2C Address */
#define VEML7700_I2C_ADDR    0x10    // Fixed I2C address

/**
 * @brief Initialize VEML7700 sensor
 * 
 * @param i2c_bus I2C bus handle (Bus 2)
 * @return ESP_OK on success, ESP_FAIL otherwise
 */
esp_err_t veml7700_init(i2c_bus_handle_t i2c_bus);

/**
 * @brief Read ambient light level in lux
 * 
 * @param lux Pointer to store light level in lux
 * @return ESP_OK on success
 */
esp_err_t veml7700_read_lux(float *lux);

/**
 * @brief Read raw ALS (Ambient Light Sensor) value
 * 
 * @param als_raw Pointer to store raw ALS count
 * @return ESP_OK on success
 */
esp_err_t veml7700_read_als_raw(uint16_t *als_raw);

/**
 * @brief Enable power saving mode
 * 
 * @return ESP_OK on success
 */
esp_err_t veml7700_power_down(void);

/**
 * @brief Wake from power saving mode
 * 
 * @return ESP_OK on success
 */
esp_err_t veml7700_power_up(void);

#ifdef __cplusplus
}
#endif
