#include "sensor_if.h"
#include "sht41.h"
#include "lps22hb.h"
#include "esp_log.h"
#include "i2c_bus.h"
#include <stdio.h>

static const char *TAG = "SENSOR_IF";
static sensor_type_t detected = SENSOR_TYPE_NONE;
static bool sht41_available = false;
static bool lps22hb_available = false;

esp_err_t sensor_init(i2c_bus_handle_t i2c_bus)
{
    if (!i2c_bus) {
        ESP_LOGW(TAG, "sensor_init: i2c_bus handle is NULL");
        return ESP_ERR_INVALID_ARG;
    }

    detected = SENSOR_TYPE_NONE;
    sht41_available = false;
    lps22hb_available = false;

    // Diagnostic scan: list all devices on the bus to help debug NACKs
    uint8_t found[32];
    int n = i2c_bus_scan(i2c_bus, found, sizeof(found));
    if (n == 0) {
        ESP_LOGW(TAG, "I2C scan: no devices found on bus");
    } else {
        char buf[128];
        int off = 0;
        off += snprintf(buf + off, sizeof(buf) - off, "I2C scan: %d device(s):", n);
        for (int i = 0; i < n; ++i) off += snprintf(buf + off, sizeof(buf) - off, " 0x%02x", found[i]);
        ESP_LOGI(TAG, "%s", buf);
    }

    ESP_LOGI(TAG, "Probing for SHT4x...");
    if (sht41_init(i2c_bus) == ESP_OK) {
        sht41_available = true;
        ESP_LOGI(TAG, "SHT4x probe OK");
    } else {
        ESP_LOGW(TAG, "SHT4x not found or init failed");
    }

    ESP_LOGI(TAG, "Probing for LPS22HB...");
    if (lps22hb_init(i2c_bus) == ESP_OK) {
        lps22hb_available = true;
        ESP_LOGI(TAG, "LPS22HB probe OK");
    } else {
        ESP_LOGW(TAG, "LPS22HB not found or init failed");
    }

    if (sht41_available && lps22hb_available) {
        detected = SENSOR_TYPE_SHT41_LPS22HB;
        ESP_LOGI(TAG, "Detected sensor combo: SHT4x + LPS22HB");
        return ESP_OK;
    }
    if (sht41_available) {
        detected = SENSOR_TYPE_SHT41;
        ESP_LOGI(TAG, "Detected sensor: SHT4x only (humidity + temperature)");
        return ESP_OK;
    }
    if (lps22hb_available) {
        detected = SENSOR_TYPE_LPS22HB;
        ESP_LOGI(TAG, "Detected sensor: LPS22HB only (pressure + temperature)");
        return ESP_OK;
    }

    return ESP_ERR_NOT_FOUND;
}

sensor_type_t sensor_get_type(void)
{
    return detected;
}

esp_err_t sensor_wake_and_measure(void)
{
    esp_err_t r1 = ESP_ERR_NOT_FOUND;
    esp_err_t r2 = ESP_ERR_NOT_FOUND;

    if (sht41_available) {
        r1 = sht41_trigger_measurement();
    }
    if (lps22hb_available) {
        r2 = lps22hb_trigger_measurement();
    }

    return (r1 == ESP_OK || r2 == ESP_OK) ? ESP_OK : ESP_ERR_NOT_FOUND;
}

esp_err_t sensor_read_temperature(float *out_c)
{
    if (!out_c) return ESP_ERR_INVALID_ARG;
    if (sht41_available) {
        return sht41_read_temperature(out_c);
    }
    if (lps22hb_available) {
        return lps22hb_read_temperature(out_c);
    }
    return ESP_ERR_NOT_FOUND;
}

esp_err_t sensor_read_humidity(float *out_percent)
{
    if (!out_percent) return ESP_ERR_INVALID_ARG;
    if (sht41_available) {
        return sht41_read_humidity(out_percent);
    }
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t sensor_read_pressure(float *out_hpa)
{
    if (!out_hpa) return ESP_ERR_INVALID_ARG;
    if (lps22hb_available) {
        return lps22hb_read_pressure(out_hpa);
    }
    return ESP_ERR_NOT_SUPPORTED;
}
