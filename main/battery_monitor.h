/*
 * Battery Monitoring for Hardware v2.0
 * Smart battery voltage measurement with MOSFET control
 */

#pragma once

#include "esp_err.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize battery monitoring system
 * 
 * Configures:
 * - GPIO3 as output for MOSFET control (enable measurement)
 * - GPIO4 (ADC1_CH4) for voltage reading
 * - ADC calibration for accurate readings
 * 
 * @return ESP_OK on success, ESP_FAIL otherwise
 */
esp_err_t battery_monitor_init(void);

/**
 * @brief Read battery voltage
 * 
 * Enables MOSFETs, takes ADC reading, disables MOSFETs
 * 
 * @param voltage_mv Pointer to store voltage in millivolts
 * @return ESP_OK on success, ESP_FAIL otherwise
 */
esp_err_t battery_read_voltage(uint16_t *voltage_mv);

/**
 * @brief Calculate battery percentage from voltage
 * 
 * Uses Li-Ion discharge curve (2.7V = 0%, 4.2V = 100%)
 * 
 * @param voltage_mv Voltage in millivolts
 * @return Battery percentage (0-100)
 */
uint8_t battery_voltage_to_percentage(uint16_t voltage_mv);

/**
 * @brief Get battery voltage for Zigbee reporting (0.1V units)
 * 
 * @return Voltage in 0.1V units (e.g., 37 = 3.7V)
 */
uint8_t battery_get_zigbee_voltage(void);

/**
 * @brief Get battery percentage for Zigbee reporting (doubled)
 * 
 * @return Percentage * 2 (Zigbee spec: 200 = 100%)
 */
uint8_t battery_get_zigbee_percentage(void);

#ifdef __cplusplus
}
#endif
