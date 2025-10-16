/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier:  LicenseRef-Included
 *
 * ESP32-H2 Deep Sleep Management for Weather Station
 *
 * This file implements deep sleep functionality for battery-powered operation
 */

#include "esp_sleep.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "driver/rtc_io.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_zigbee_core.h"
#include "esp_zb_weather.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *SLEEP_TAG = "SLEEP";

/* RTC memory to persist data across sleep cycles */
static RTC_DATA_ATTR uint32_t boot_count = 0;
static RTC_DATA_ATTR float rtc_rainfall_mm = 0.0f;
static RTC_DATA_ATTR uint32_t rtc_rain_pulse_count = 0;
static RTC_DATA_ATTR int64_t last_report_timestamp = 0;

/* Wake-up reason tracking */
typedef enum {
    WAKE_REASON_TIMER = 0,
    WAKE_REASON_RAIN,
    WAKE_REASON_BUTTON,
    WAKE_REASON_RESET
} wake_reason_t;

/**
 * @brief Get and log the wake-up reason
 * @return wake_reason_t The reason for wake-up
 */
wake_reason_t check_wake_reason(void)
{
    esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();
    
    boot_count++;
    ESP_LOGI(SLEEP_TAG, "🔄 Wake-up #%lu", boot_count);
    
    switch (wakeup_reason) {
        case ESP_SLEEP_WAKEUP_TIMER:
            ESP_LOGI(SLEEP_TAG, "⏰ Wake-up reason: TIMER (periodic 15-minute interval)");
            return WAKE_REASON_TIMER;
            
        case ESP_SLEEP_WAKEUP_EXT0:
        case ESP_SLEEP_WAKEUP_EXT1:
            ESP_LOGI(SLEEP_TAG, "🌧️ Wake-up reason: RAIN DETECTED on GPIO%d", RAIN_WAKE_GPIO);
            return WAKE_REASON_RAIN;
            
        case ESP_SLEEP_WAKEUP_GPIO:
            ESP_LOGI(SLEEP_TAG, "🔘 Wake-up reason: BUTTON press");
            return WAKE_REASON_BUTTON;
            
        case ESP_SLEEP_WAKEUP_UNDEFINED:
        default:
            ESP_LOGI(SLEEP_TAG, "🔌 Wake-up reason: POWER ON / RESET");
            // Reset RTC memory on first boot
            if (boot_count == 1) {
                rtc_rainfall_mm = 0.0f;
                rtc_rain_pulse_count = 0;
                last_report_timestamp = 0;
            }
            return WAKE_REASON_RESET;
    }
}

/**
 * @brief Configure GPIO for wake-up from deep sleep
 * @param gpio_num GPIO number to use for wake-up
 * @param level Wake-up level (0=low, 1=high)
 */
void configure_gpio_wakeup(gpio_num_t gpio_num, int level)
{
    ESP_LOGI(SLEEP_TAG, "Configuring GPIO%d for wake-up on %s", gpio_num, level ? "HIGH" : "LOW");
    
    /* For ESP32-H2, we need to use ext1 wake-up for GPIO wake-up */
    /* Configure the GPIO as input with pull-down (for active-high rain gauge) */
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << gpio_num),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,  // Pull down so HIGH pulse wakes us up
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&io_conf);
    
    /* Enable wake-up on this GPIO going HIGH */
    esp_sleep_enable_ext1_wakeup((1ULL << gpio_num), ESP_EXT1_WAKEUP_ANY_HIGH);
    
    ESP_LOGI(SLEEP_TAG, "✅ GPIO%d wake-up configured (trigger on HIGH)", gpio_num);
}

/**
 * @brief Save rainfall data to RTC memory and NVS
 * @param rainfall_mm Current total rainfall in mm
 * @param pulse_count Current pulse count
 */
void save_rainfall_data(float rainfall_mm, uint32_t pulse_count)
{
    // Save to RTC memory (fast, survives sleep)
    rtc_rainfall_mm = rainfall_mm;
    rtc_rain_pulse_count = pulse_count;
    
    ESP_LOGI(SLEEP_TAG, "💾 Saved to RTC: %.2f mm, %lu pulses", rtc_rainfall_mm, rtc_rain_pulse_count);
    
    // Also save to NVS for persistence across power loss
    nvs_handle_t nvs_handle;
    esp_err_t ret = nvs_open("rain_storage", NVS_READWRITE, &nvs_handle);
    if (ret == ESP_OK) {
        nvs_set_blob(nvs_handle, "rainfall", &rainfall_mm, sizeof(float));
        nvs_set_u32(nvs_handle, "pulses", pulse_count);
        nvs_commit(nvs_handle);
        nvs_close(nvs_handle);
        ESP_LOGI(SLEEP_TAG, "💾 Saved to NVS: %.2f mm, %lu pulses", rainfall_mm, pulse_count);
    }
}

/**
 * @brief Load rainfall data from RTC memory or NVS
 * @param rainfall_mm Pointer to store rainfall value
 * @param pulse_count Pointer to store pulse count
 * @return true if data loaded from RTC, false if loaded from NVS
 */
bool load_rainfall_data(float *rainfall_mm, uint32_t *pulse_count)
{
    // Check if we have valid RTC data (from previous sleep cycle)
    if (boot_count > 1 && rtc_rainfall_mm >= 0.0f) {
        *rainfall_mm = rtc_rainfall_mm;
        *pulse_count = rtc_rain_pulse_count;
        ESP_LOGI(SLEEP_TAG, "📂 Loaded from RTC: %.2f mm, %lu pulses", *rainfall_mm, *pulse_count);
        return true;
    }
    
    // Otherwise, try to load from NVS
    nvs_handle_t nvs_handle;
    esp_err_t ret = nvs_open("rain_storage", NVS_READONLY, &nvs_handle);
    if (ret == ESP_OK) {
        size_t size = sizeof(float);
        ret = nvs_get_blob(nvs_handle, "rainfall", rainfall_mm, &size);
        if (ret == ESP_OK) {
            nvs_get_u32(nvs_handle, "pulses", pulse_count);
            nvs_close(nvs_handle);
            ESP_LOGI(SLEEP_TAG, "📂 Loaded from NVS: %.2f mm, %lu pulses", *rainfall_mm, *pulse_count);
            
            // Update RTC memory
            rtc_rainfall_mm = *rainfall_mm;
            rtc_rain_pulse_count = *pulse_count;
            return false;
        }
        nvs_close(nvs_handle);
    }
    
    // No stored data, start fresh
    *rainfall_mm = 0.0f;
    *pulse_count = 0;
    ESP_LOGI(SLEEP_TAG, "📂 No stored data, starting from 0.0 mm");
    return false;
}

/**
 * @brief Enter deep sleep mode
 * @param duration_seconds Sleep duration in seconds (0 = infinite)
 * @param enable_gpio_wakeup Enable GPIO wake-up for rain detection
 */
void enter_deep_sleep(uint32_t duration_seconds, bool enable_gpio_wakeup)
{
    ESP_LOGI(SLEEP_TAG, "🌙 Preparing to enter deep sleep...");
    
    /* Configure timer wake-up if duration specified */
    if (duration_seconds > 0) {
        ESP_LOGI(SLEEP_TAG, "⏰ Timer wake-up: %lu seconds (%lu minutes)", 
                 duration_seconds, duration_seconds / 60);
        esp_sleep_enable_timer_wakeup(duration_seconds * 1000000ULL); // microseconds
    }
    
    /* Configure GPIO wake-up for rain detection */
    if (enable_gpio_wakeup) {
        configure_gpio_wakeup(RAIN_WAKE_GPIO, 1); // Wake on HIGH (rain pulse)
    }
    
    /* Disable LEDs to save power */
    ESP_LOGI(SLEEP_TAG, "💡 Disabling LEDs...");
    // LED strip and GPIO LED should already be off
    
    /* Print power consumption estimate */
    ESP_LOGI(SLEEP_TAG, "📊 Expected sleep current: 7-10 µA");
    ESP_LOGI(SLEEP_TAG, "🔋 Battery life estimate: 3+ years with 2500mAh battery");
    
    /* Print wake-up configuration */
    ESP_LOGI(SLEEP_TAG, "Wake-up sources enabled:");
    if (duration_seconds > 0) {
        ESP_LOGI(SLEEP_TAG, "  ⏰ Timer: every %lu minutes", duration_seconds / 60);
    }
    if (enable_gpio_wakeup) {
        ESP_LOGI(SLEEP_TAG, "  🌧️ Rain: GPIO%d (>%.1f mm)", RAIN_WAKE_GPIO, RAIN_MM_THRESHOLD);
    }
    
    /* Flush all logs before sleeping */
    esp_log_level_set("*", ESP_LOG_INFO);
    vTaskDelay(pdMS_TO_TICKS(100)); // Give time for logs to flush
    
    ESP_LOGI(SLEEP_TAG, "💤 Entering deep sleep NOW...");
    ESP_LOGI(SLEEP_TAG, "========================================");
    vTaskDelay(pdMS_TO_TICKS(100));
    
    /* Enter deep sleep */
    esp_deep_sleep_start();
    
    /* This line is never reached */
}

/**
 * @brief Calculate power consumption estimate
 * @param battery_mah Battery capacity in mAh
 * @return Estimated battery life in days
 */
uint32_t estimate_battery_life(uint32_t battery_mah)
{
    /* Power consumption per day:
     * - 96 wake cycles (every 15 minutes)
     * - 20 mA active for 3 seconds per cycle = 0.017 mAh per cycle
     * - 10 µA sleep for 897 seconds per cycle = 0.0025 mAh per cycle
     * Total per cycle: ~0.02 mAh
     * Total per day: 96 * 0.02 = 1.92 mAh
     */
    const float MAH_PER_DAY = 2.1f; // Including overhead
    uint32_t days = (uint32_t)(battery_mah / MAH_PER_DAY);
    
    ESP_LOGI(SLEEP_TAG, "🔋 Battery capacity: %lu mAh", battery_mah);
    ESP_LOGI(SLEEP_TAG, "📊 Daily consumption: %.2f mAh", MAH_PER_DAY);
    ESP_LOGI(SLEEP_TAG, "📅 Estimated battery life: %lu days (~%.1f years)", 
             days, days / 365.0f);
    
    return days;
}

/**
 * @brief Get sleep duration based on rainfall
 * @param recent_rainfall_mm Recent rainfall in last sleep cycle
 * @return Sleep duration in seconds
 */
uint32_t get_adaptive_sleep_duration(float recent_rainfall_mm)
{
    /* If it's raining heavily, check more frequently */
    if (recent_rainfall_mm > RAIN_MM_THRESHOLD) {
        ESP_LOGI(SLEEP_TAG, "🌧️ Recent rain detected (%.2f mm), using shorter sleep (5 min)", 
                 recent_rainfall_mm);
        return 5 * 60; // 5 minutes during active rain
    }
    
    /* Normal sleep duration */
    return SLEEP_DURATION_S; // 15 minutes default
}

/**
 * @brief Print wake-up statistics
 */
void print_wake_statistics(void)
{
    ESP_LOGI(SLEEP_TAG, "========================================");
    ESP_LOGI(SLEEP_TAG, "📊 WAKE-UP STATISTICS");
    ESP_LOGI(SLEEP_TAG, "========================================");
    ESP_LOGI(SLEEP_TAG, "Boot count: %lu", boot_count);
    ESP_LOGI(SLEEP_TAG, "Rainfall (RTC): %.2f mm", rtc_rainfall_mm);
    ESP_LOGI(SLEEP_TAG, "Pulse count (RTC): %lu", rtc_rain_pulse_count);
    
    /* Calculate uptime percentage */
    /* Awake ~3 sec every 15 min = 3/900 = 0.33% duty cycle */
    ESP_LOGI(SLEEP_TAG, "Duty cycle: ~0.3%% (awake ~3s per 15min)");
    ESP_LOGI(SLEEP_TAG, "Sleep efficiency: ~99.7%%");
    ESP_LOGI(SLEEP_TAG, "========================================");
}
