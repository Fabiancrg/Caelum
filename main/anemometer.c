/*
 * Anemometer Driver - Honeywell SS445P Hall Effect Sensor
 * Measures wind speed via pulse counting
 */

#include "anemometer.h"
#include "esp_zb_weather.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"

static const char *TAG = "ANEMOMETER";

/* Pulse counting state */
static volatile uint32_t pulse_count = 0;
static int64_t last_measurement_time_us = 0;
static bool interrupts_enabled = false;

/* ISR handler for anemometer pulses */
static void IRAM_ATTR anemometer_isr_handler(void *arg)
{
    pulse_count++;
}

esp_err_t anemometer_init(void)
{
    /* Configure GPIO for interrupt */
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << ANEMOMETER_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_NEGEDGE,  // SS445P pulls low on magnet detection
    };
    
    esp_err_t ret = gpio_config(&io_conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure GPIO");
        return ret;
    }

    /* Install ISR service */
    ret = gpio_install_isr_service(0);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "Failed to install ISR service");
        return ret;
    }

    /* Add ISR handler */
    ret = gpio_isr_handler_add(ANEMOMETER_GPIO, anemometer_isr_handler, NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add ISR handler");
        return ret;
    }

    interrupts_enabled = true;
    last_measurement_time_us = esp_timer_get_time();
    pulse_count = 0;

    ESP_LOGI(TAG, "Anemometer initialized on GPIO%d", ANEMOMETER_GPIO);
    return ESP_OK;
}

esp_err_t anemometer_get_wind_speed(float *speed_ms)
{
    if (!speed_ms) return ESP_ERR_INVALID_ARG;

    /* Get current time and calculate elapsed time */
    int64_t current_time_us = esp_timer_get_time();
    int64_t elapsed_us = current_time_us - last_measurement_time_us;
    
    if (elapsed_us <= 0) {
        *speed_ms = 0.0f;
        return ESP_OK;
    }

    /* Get pulse count and reset */
    uint32_t pulses = pulse_count;
    pulse_count = 0;
    last_measurement_time_us = current_time_us;

    /* Calculate wind speed */
    /* v = (pulses / elapsed_time) * (2π * radius) * calibration */
    float elapsed_s = elapsed_us / 1000000.0f;
    float rotations_per_sec = pulses / (ANEMOMETER_PULSES_PER_REV * elapsed_s);
    float circumference = 2.0f * 3.14159f * ANEMOMETER_RADIUS_M;
    *speed_ms = rotations_per_sec * circumference * ANEMOMETER_CALIBRATION;

    ESP_LOGI(TAG, "Wind speed: %.2f m/s (%d pulses in %.1fs)", 
             *speed_ms, pulses, elapsed_s);

    return ESP_OK;
}

void anemometer_reset(void)
{
    pulse_count = 0;
    last_measurement_time_us = esp_timer_get_time();
}

void anemometer_enable(void)
{
    if (!interrupts_enabled) {
        gpio_intr_enable(ANEMOMETER_GPIO);
        interrupts_enabled = true;
        anemometer_reset();
    }
}

void anemometer_disable(void)
{
    if (interrupts_enabled) {
        gpio_intr_disable(ANEMOMETER_GPIO);
        interrupts_enabled = false;
    }
}
