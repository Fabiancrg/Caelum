/*
 * AS5600 Magnetic Position Sensor Driver
 * 12-bit contactless magnetic angle sensor for wind direction
 */

#pragma once

#include "i2c_bus.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* AS5600 I2C Address */
#define AS5600_I2C_ADDR    0x36    // Fixed I2C address

/**
 * @brief Initialize AS5600 sensor
 * 
 * @param i2c_bus I2C bus handle (Bus 2)
 * @return ESP_OK on success, ESP_FAIL otherwise
 */
esp_err_t as5600_init(i2c_bus_handle_t i2c_bus);

/**
 * @brief Read raw angle from AS5600
 * 
 * @param angle Pointer to store raw angle (0-4095, 12-bit)
 * @return ESP_OK on success
 */
esp_err_t as5600_read_angle_raw(uint16_t *angle);

/**
 * @brief Read angle in degrees (0-360°)
 * 
 * @param degrees Pointer to store angle in degrees
 * @return ESP_OK on success
 */
esp_err_t as5600_read_angle_degrees(float *degrees);

/**
 * @brief Get wind direction in compass degrees (0° = North)
 * 
 * Applies calibration offset for physical installation orientation
 * 
 * @param direction Pointer to store wind direction (0-360°)
 * @return ESP_OK on success
 */
esp_err_t as5600_get_wind_direction(float *direction);

/**
 * @brief Check magnet detection status
 * 
 * @param detected Pointer to store detection status (true = magnet detected)
 * @return ESP_OK on success
 */
esp_err_t as5600_check_magnet(bool *detected);

#ifdef __cplusplus
}
#endif
