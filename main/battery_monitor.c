/*
 * Battery Monitoring for Hardware v2.0
 * Smart battery voltage measurement with MOSFET control
 */

#include "battery_monitor.h"
#include "esp_zb_weather.h"
#include "esp_log.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "BATTERY";

static adc_oneshot_unit_handle_t adc_handle = NULL;
static adc_cali_handle_t adc_cali_handle = NULL;
static bool adc_calibrated = false;

/* Last measured values */
static uint16_t last_voltage_mv = 0;
static uint8_t last_percentage = 0;

/* MOSFET control timing */
#define MOSFET_SETTLE_TIME_MS  10    // Time for voltage to stabilize after enabling MOSFETs

esp_err_t battery_monitor_init(void)
{
    esp_err_t ret;

    /* Configure MOSFET enable GPIO */
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << BATTERY_ENABLE_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ret = gpio_config(&io_conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure MOSFET enable GPIO");
        return ret;
    }
    
    /* Start with MOSFETs disabled (low power) */
    gpio_set_level(BATTERY_ENABLE_GPIO, 0);
    ESP_LOGI(TAG, "MOSFET control configured on GPIO%d", BATTERY_ENABLE_GPIO);

    /* Configure ADC */
    adc_oneshot_unit_init_cfg_t init_config = {
        .unit_id = ADC_UNIT_1,
    };
    ret = adc_oneshot_new_unit(&init_config, &adc_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize ADC unit");
        return ret;
    }

    adc_oneshot_chan_cfg_t config = {
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    ret = adc_oneshot_config_channel(adc_handle, BATTERY_ADC_CHANNEL, &config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure ADC channel");
        return ret;
    }

    /* Initialize ADC calibration */
#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    adc_cali_curve_fitting_config_t cali_config = {
        .unit_id = ADC_UNIT_1,
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    ret = adc_cali_create_scheme_curve_fitting(&cali_config, &adc_cali_handle);
    if (ret == ESP_OK) {
        adc_calibrated = true;
        ESP_LOGI(TAG, "ADC calibration initialized (curve fitting)");
    }
#elif ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
    adc_cali_line_fitting_config_t cali_config = {
        .unit_id = ADC_UNIT_1,
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    ret = adc_cali_create_scheme_line_fitting(&cali_config, &adc_cali_handle);
    if (ret == ESP_OK) {
        adc_calibrated = true;
        ESP_LOGI(TAG, "ADC calibration initialized (line fitting)");
    }
#endif

    if (!adc_calibrated) {
        ESP_LOGW(TAG, "ADC calibration not available, using raw values");
    }

    ESP_LOGI(TAG, "Battery monitor initialized (GPIO%d enable, GPIO%d ADC)", 
             BATTERY_ENABLE_GPIO, BATTERY_ADC_GPIO);

    return ESP_OK;
}

esp_err_t battery_read_voltage(uint16_t *voltage_mv)
{
    if (!voltage_mv) return ESP_ERR_INVALID_ARG;

    /* Enable MOSFETs to connect battery to voltage divider */
    gpio_set_level(BATTERY_ENABLE_GPIO, 1);
    vTaskDelay(pdMS_TO_TICKS(MOSFET_SETTLE_TIME_MS));

    /* Take multiple ADC samples for accuracy */
    int adc_raw_sum = 0;
    const int num_samples = 3;
    
    for (int i = 0; i < num_samples; i++) {
        int adc_raw;
        esp_err_t ret = adc_oneshot_read(adc_handle, BATTERY_ADC_CHANNEL, &adc_raw);
        if (ret != ESP_OK) {
            gpio_set_level(BATTERY_ENABLE_GPIO, 0);  // Disable MOSFETs
            ESP_LOGE(TAG, "ADC read failed");
            return ret;
        }
        adc_raw_sum += adc_raw;
        vTaskDelay(pdMS_TO_TICKS(1));
    }

    /* Disable MOSFETs to save power */
    gpio_set_level(BATTERY_ENABLE_GPIO, 0);

    /* Calculate average */
    int adc_raw_avg = adc_raw_sum / num_samples;

    /* Convert to voltage */
    int voltage_divider_mv;
    if (adc_calibrated) {
        esp_err_t ret = adc_cali_raw_to_voltage(adc_cali_handle, adc_raw_avg, &voltage_divider_mv);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "ADC calibration failed");
            return ret;
        }
    } else {
        /* Fallback: approximate conversion for ESP32-H2 */
        voltage_divider_mv = (adc_raw_avg * 3300) / 4095;
    }

    /* Account for voltage divider (R1 + R2) / R2 */
    uint32_t battery_mv = (voltage_divider_mv * (BATTERY_VOLTAGE_DIVIDER_R1 + BATTERY_VOLTAGE_DIVIDER_R2)) / BATTERY_VOLTAGE_DIVIDER_R2;

    *voltage_mv = (uint16_t)battery_mv;
    last_voltage_mv = *voltage_mv;
    last_percentage = battery_voltage_to_percentage(*voltage_mv);

    ESP_LOGI(TAG, "Battery: %d mV (%d%%) [ADC raw: %d, divider: %d mV]", 
             *voltage_mv, last_percentage, adc_raw_avg, voltage_divider_mv);

    return ESP_OK;
}

uint8_t battery_voltage_to_percentage(uint16_t voltage_mv)
{
    /* Li-Ion discharge curve: 2700mV = 0%, 4200mV = 100% */
    const uint16_t v_min = 2700;
    const uint16_t v_max = 4200;

    if (voltage_mv <= v_min) return 0;
    if (voltage_mv >= v_max) return 100;

    return (uint8_t)(((voltage_mv - v_min) * 100) / (v_max - v_min));
}

uint8_t battery_get_zigbee_voltage(void)
{
    /* Return voltage in 0.1V units (e.g., 37 = 3.7V) */
    return (uint8_t)(last_voltage_mv / 100);
}

uint8_t battery_get_zigbee_percentage(void)
{
    /* Zigbee spec: percentage * 2 (200 = 100%) */
    return last_percentage * 2;
}
