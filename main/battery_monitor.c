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

/* Diagnostics from the last read, surfaced over Zigbee so the battery path can
 * be debugged on a battery-only device (no USB serial available). */
static uint16_t last_raw_adc = 0;       // averaged/median raw ADC count
static uint16_t last_divider_mv = 0;    // voltage at the ADC pin (pre divider-scaling)
static bool     last_calibrated = false; // was ADC calibration applied (vs crude fallback)?

/* MOSFET control timing */
#define MOSFET_SETTLE_TIME_MS  10    // Time for voltage to stabilize after enabling MOSFETs

esp_err_t battery_monitor_init(void)
{
    esp_err_t ret;

    /* Already initialized: keep the existing ADC unit + calibration handles.
     * The ADC and its calibration scheme are created ONCE and reused for the
     * lifetime of the device. We intentionally do NOT tear them down after each
     * read - repeatedly recreating the curve-fitting calibration was unreliable
     * (it could silently fail after many sleep/wake cycles, dropping us to the
     * crude raw*3300/4095 fallback which under-reads ~20% -> bogus ~3.3 V). */
    if (adc_handle != NULL) {
        return ESP_OK;
    }

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

    /* Re-init ADC if it was released after previous read */
    if (adc_handle == NULL) {
        esp_err_t ret = battery_monitor_init();
        if (ret != ESP_OK) {
            return ret;
        }
    }

    /* Enable MOSFETs to connect battery to voltage divider */
    gpio_set_level(BATTERY_ENABLE_GPIO, 1);
    vTaskDelay(pdMS_TO_TICKS(MOSFET_SETTLE_TIME_MS));

    /* Take several bursts spread over ~100 ms and use the MEDIAN. A transient dip
     * (e.g. the cell sagging during a radio TX spike, or an ADC glitch) on one or
     * two bursts then cannot drag the result down. Previously a single bad sample
     * produced a spurious "empty battery" value that got cached for an hour. */
    const int num_bursts = 7;
    int burst_raw[7];
    for (int b = 0; b < num_bursts; b++) {
        int sum = 0;
        const int sub = 3;
        for (int i = 0; i < sub; i++) {
            int adc_raw;
            esp_err_t ret = adc_oneshot_read(adc_handle, BATTERY_ADC_CHANNEL, &adc_raw);
            if (ret != ESP_OK) {
                gpio_set_level(BATTERY_ENABLE_GPIO, 0);  // Disable MOSFETs
                ESP_LOGE(TAG, "ADC read failed");
                return ret;
            }
            sum += adc_raw;
        }
        burst_raw[b] = sum / sub;
        vTaskDelay(pdMS_TO_TICKS(15));  // ~15 ms between bursts => ~100 ms total window
    }

    /* Disable MOSFETs to save power */
    gpio_set_level(BATTERY_ENABLE_GPIO, 0);

    /* Median of the bursts (insertion sort of a small array, then middle value). */
    for (int i = 1; i < num_bursts; i++) {
        int key = burst_raw[i];
        int j = i - 1;
        while (j >= 0 && burst_raw[j] > key) { burst_raw[j + 1] = burst_raw[j]; j--; }
        burst_raw[j + 1] = key;
    }
    int adc_raw_avg = burst_raw[num_bursts / 2];  // median rejects transient outliers

    /* Convert to voltage */
    int voltage_divider_mv;
    if (adc_calibrated) {
        esp_err_t ret = adc_cali_raw_to_voltage(adc_cali_handle, adc_raw_avg, &voltage_divider_mv);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "ADC calibration failed");
            return ret;
        }
    } else {
        /* Fallback: approximate conversion for ESP32-H2. NOTE: this under-reads
         * by ~20% vs the calibrated value and should essentially never run now
         * that calibration is created once and kept - warn loudly if it does. */
        voltage_divider_mv = (adc_raw_avg * 3300) / 4095;
        ESP_LOGW(TAG, "⚠️ ADC calibration unavailable - using crude fallback (battery voltage will read low!)");
    }

    /* Account for voltage divider (R1 + R2) / R2 */
    uint32_t battery_mv = (voltage_divider_mv * (BATTERY_VOLTAGE_DIVIDER_R1 + BATTERY_VOLTAGE_DIVIDER_R2)) / BATTERY_VOLTAGE_DIVIDER_R2;

    *voltage_mv = (uint16_t)battery_mv;
    last_voltage_mv = *voltage_mv;
    last_percentage = battery_voltage_to_percentage(*voltage_mv);
    last_raw_adc = (uint16_t)adc_raw_avg;
    last_divider_mv = (uint16_t)voltage_divider_mv;
    last_calibrated = adc_calibrated;

    ESP_LOGI(TAG, "Battery: %d mV (%d%%) [ADC raw: %d, divider: %d mV, cal: %s]",
             *voltage_mv, last_percentage, adc_raw_avg, voltage_divider_mv,
             adc_calibrated ? "yes" : "NO");

    /* The ADC unit and calibration scheme are intentionally kept alive between
     * reads (see battery_monitor_init). Only the divider is disconnected from the
     * battery, via the MOSFET enable GPIO already set low above - that is what
     * saves power. adc_oneshot draws no meaningful current while idle. */

    return ESP_OK;
}

uint8_t battery_voltage_to_percentage(uint16_t voltage_mv)
{
    /* Piecewise-linear Li-Ion (LiCoO2/LiPo) discharge curve mapping resting cell
     * voltage to state-of-charge. Far more faithful than a straight line: the cell
     * spends most of its life on a flat ~3.7-3.9 V plateau and then drops quickly
     * at both ends. Table is descending by voltage; we linearly interpolate
     * between adjacent breakpoints. (A straight 2.7-4.2 V line badly over-reads in
     * the mid-range and hides the end-of-life cliff.) */
    static const struct { uint16_t mv; uint8_t pct; } curve[] = {
        {4200, 100}, {4150, 95}, {4110, 90}, {4080, 85}, {4020, 80},
        {3980,  75}, {3950, 70}, {3910, 65}, {3870, 60}, {3850, 55},
        {3840,  50}, {3820, 45}, {3800, 40}, {3790, 35}, {3770, 30},
        {3750,  25}, {3730, 20}, {3710, 15}, {3690, 10}, {3610,  5},
        {3270,   0},
    };
    const int n = sizeof(curve) / sizeof(curve[0]);

    if (voltage_mv >= curve[0].mv)     return curve[0].pct;       /* >= 4.20 V -> 100% */
    if (voltage_mv <= curve[n - 1].mv) return curve[n - 1].pct;   /* <= 3.27 V -> 0%   */

    for (int i = 0; i < n - 1; i++) {
        uint16_t v_hi = curve[i].mv;
        uint16_t v_lo = curve[i + 1].mv;
        if (voltage_mv <= v_hi && voltage_mv > v_lo) {
            uint8_t p_hi = curve[i].pct;
            uint8_t p_lo = curve[i + 1].pct;
            return (uint8_t)(p_lo + ((uint32_t)(voltage_mv - v_lo) * (p_hi - p_lo)) / (v_hi - v_lo));
        }
    }
    return 0; /* unreachable */
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

/* Diagnostics from the last read (surfaced over Zigbee). */
uint16_t battery_get_last_raw_adc(void)    { return last_raw_adc; }
uint16_t battery_get_last_divider_mv(void) { return last_divider_mv; }
bool     battery_get_last_calibrated(void) { return last_calibrated; }
