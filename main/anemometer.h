/*
 * Anemometer Driver - Honeywell SS445P Hall Effect Sensor
 * Measures wind speed via pulse counting
 */

#pragma once

#include "esp_err.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Wind speed calculation constants */
#define ANEMOMETER_RADIUS_M        0.07f    // 70mm radius (adjust to your anemometer)
#define ANEMOMETER_PULSES_PER_REV  1        // SS445P generates 1 pulse per revolution
#define ANEMOMETER_CALIBRATION     1.18f    // Calibration factor (adjust based on testing)

/**
 * @brief Initialize anemometer
 * 
 * Configures GPIO14 with interrupt for pulse counting
 * 
 * @return ESP_OK on success, ESP_FAIL otherwise
 */
esp_err_t anemometer_init(void);

/**
 * @brief Get current wind speed
 * 
 * Calculates wind speed based on pulse count over time period
 * 
 * @param speed_ms Pointer to store wind speed in m/s
 * @return ESP_OK on success
 */
esp_err_t anemometer_get_wind_speed(float *speed_ms);

/**
 * @brief Reset pulse counter
 * 
 * Resets accumulated pulse count and timestamp
 */
void anemometer_reset(void);

/**
 * @brief Enable anemometer interrupts
 */
void anemometer_enable(void);

/**
 * @brief Disable anemometer interrupts (power saving)
 */
void anemometer_disable(void);

#ifdef __cplusplus
}
#endif
