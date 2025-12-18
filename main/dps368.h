/*
 * DPS368 Pressure Sensor Driver
 * High-precision barometric pressure sensor
 */

#pragma once

#include "i2c_bus.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* DPS368 I2C Address */
#define DPS368_I2C_ADDR    0x77    // Default address (can be 0x76 with SDO low)

/**
 * @brief Initialize DPS368 sensor
 * 
 * @param i2c_bus I2C bus handle (Bus 1)
 * @return ESP_OK on success, ESP_FAIL otherwise
 */
esp_err_t dps368_init(i2c_bus_handle_t i2c_bus);

/**
 * @brief Read temperature from DPS368
 * 
 * @param temperature Pointer to store temperature in °C
 * @return ESP_OK on success
 */
esp_err_t dps368_read_temperature(float *temperature);

/**
 * @brief Read pressure from DPS368
 * 
 * @param pressure Pointer to store pressure in hPa
 * @return ESP_OK on success
 */
esp_err_t dps368_read_pressure(float *pressure);

/**
 * @brief Trigger measurement (if in one-shot mode)
 * 
 * @return ESP_OK on success
 */
esp_err_t dps368_trigger_measurement(void);

#ifdef __cplusplus
}
#endif
