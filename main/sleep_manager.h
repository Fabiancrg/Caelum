/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier:  LicenseRef-Included
 *
 * ESP32-H2 Deep Sleep Management Header
 */

#ifndef SLEEP_MANAGER_H
#define SLEEP_MANAGER_H

#include <stdint.h>
#include <stdbool.h>
#include "driver/gpio.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Wake-up reason enumeration
 */
typedef enum {
    WAKE_REASON_TIMER = 0,      /*!< Woke up from timer (15-minute interval) */
    WAKE_REASON_RAIN,           /*!< Woke up from rain detection (GPIO) */
    WAKE_REASON_BUTTON,         /*!< Woke up from button press */
    WAKE_REASON_RESET           /*!< First boot or reset */
} wake_reason_t;

/**
 * @brief Check and return the wake-up reason
 * 
 * @return wake_reason_t The reason for wake-up
 */
wake_reason_t check_wake_reason(void);

/**
 * @brief Configure GPIO for wake-up from deep sleep
 * 
 * @param gpio_num GPIO number to monitor
 * @param level Wake-up level (0=LOW, 1=HIGH)
 */
void configure_gpio_wakeup(gpio_num_t gpio_num, int level);

/**
 * @brief Save rainfall data to RTC memory and NVS
 * 
 * @param rainfall_mm Total rainfall in millimeters
 * @param pulse_count Total pulse count
 */
void save_rainfall_data(float rainfall_mm, uint32_t pulse_count);

/**
 * @brief Load rainfall data from RTC memory or NVS
 * 
 * @param rainfall_mm Pointer to store rainfall value
 * @param pulse_count Pointer to store pulse count
 * @return true if loaded from RTC memory, false if from NVS
 */
bool load_rainfall_data(float *rainfall_mm, uint32_t *pulse_count);

/**
 * @brief Enter deep sleep mode with wake-up sources
 * 
 * @param duration_seconds Sleep duration in seconds (0 = infinite)
 * @param enable_gpio_wakeup Enable GPIO wake-up for rain detection
 */
void enter_deep_sleep(uint32_t duration_seconds, bool enable_gpio_wakeup);

/**
 * @brief Calculate estimated battery life
 * 
 * @param battery_mah Battery capacity in mAh
 * @return Estimated battery life in days
 */
uint32_t estimate_battery_life(uint32_t battery_mah);

/**
 * @brief Get adaptive sleep duration based on recent rainfall
 * 
 * @param recent_rainfall_mm Recent rainfall amount
 * @return Sleep duration in seconds
 */
uint32_t get_adaptive_sleep_duration(float recent_rainfall_mm);

/**
 * @brief Print wake-up statistics and power information
 */
void print_wake_statistics(void);

#ifdef __cplusplus
}
#endif

#endif // SLEEP_MANAGER_H
