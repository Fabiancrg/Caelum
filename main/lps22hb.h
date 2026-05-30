/*
 * LPS22HB Pressure Sensor Driver
 * MEMS barometric pressure + temperature sensor (STMicroelectronics)
 */

#pragma once

#include "i2c_bus.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* LPS22HB I2C address (SA0 = VDD). Use 0x5C if SA0 = GND. */
#define LPS22HB_I2C_ADDR    0x5D

/**
 * @brief Initialize LPS22HB sensor (one-shot / low-power mode)
 *
 * @param i2c_bus I2C bus handle (Bus 1)
 * @return ESP_OK on success, ESP_ERR_NOT_FOUND if the device is absent
 */
esp_err_t lps22hb_init(i2c_bus_handle_t i2c_bus);

/**
 * @brief Trigger a one-shot measurement and wait for completion
 *
 * @return ESP_OK on success
 */
esp_err_t lps22hb_trigger_measurement(void);

/**
 * @brief Read last measured pressure in hPa
 *
 * @param pressure Pointer to store pressure in hPa
 * @return ESP_OK on success
 */
esp_err_t lps22hb_read_pressure(float *pressure);

/**
 * @brief Read last measured temperature in degrees Celsius
 *
 * @param temperature Pointer to store temperature in °C
 * @return ESP_OK on success
 */
esp_err_t lps22hb_read_temperature(float *temperature);

#ifdef __cplusplus
}
#endif
