#include "sensor_if.h"
#include "bme280_app.h" // existing BME280 app API
#include "aht20.h"
#include "bmp280.h"
#include "sht41.h"
#include "esp_log.h"
#include "i2c_bus.h"
#include <stdio.h>

static const char *TAG = "SENSOR_IF";
static sensor_type_t detected = SENSOR_TYPE_NONE;

// Default pressure value when no pressure sensor is available (SHT41 case)
#define DEFAULT_PRESSURE_HPA 1000.0f

esp_err_t sensor_init(i2c_bus_handle_t i2c_bus)
{
    esp_err_t ret = ESP_ERR_NOT_FOUND;

    if (i2c_bus != NULL) {
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
    } else {
        ESP_LOGW(TAG, "sensor_init: i2c_bus handle is NULL");
    }

    // Try BME280 first (existing single-chip sensor)
    ESP_LOGI(TAG, "Probing for BME280...");
    ret = bme280_app_init(i2c_bus);
    if (ret == ESP_OK) {
        detected = SENSOR_TYPE_BME280;
        ESP_LOGI(TAG, "Detected sensor: BME280");
        return ESP_OK;
    }

    // Try SHT41 (temp + humidity only, no pressure)
    ESP_LOGI(TAG, "BME280 not found, probing for SHT41...");
    ret = sht41_init(i2c_bus);
    if (ret == ESP_OK) {
        detected = SENSOR_TYPE_SHT41;
        ESP_LOGI(TAG, "Detected sensor: SHT41 (pressure will default to %.1f hPa)", DEFAULT_PRESSURE_HPA);
        return ESP_OK;
    }

    // Try AHT20 + BMP280 combo
    ESP_LOGI(TAG, "SHT41 not found, probing for AHT20 + BMP280 combo...");
    ret = aht20_init(i2c_bus);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "AHT20 not present or init failed");
        return ESP_ERR_NOT_FOUND;
    }
    ret = bmp280_init(i2c_bus);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "BMP280 not present or init failed");
        return ESP_ERR_NOT_FOUND;
    }

    detected = SENSOR_TYPE_AHT20_BMP280;
    ESP_LOGI(TAG, "Detected sensor combo: AHT20 + BMP280");
    return ESP_OK;
}

sensor_type_t sensor_get_type(void)
{
    return detected;
}

esp_err_t sensor_wake_and_measure(void)
{
    if (detected == SENSOR_TYPE_BME280) {
        return bme280_app_wake_and_measure();
    } else if (detected == SENSOR_TYPE_SHT41) {
        return sht41_trigger_measurement();
    } else if (detected == SENSOR_TYPE_AHT20_BMP280) {
        // AHT20 may require a trigger; BMP280 starts measurement on read
        esp_err_t r1 = aht20_trigger_measurement();
        esp_err_t r2 = bmp280_trigger_measurement();
        return (r1 == ESP_OK || r2 == ESP_OK) ? ESP_OK : ESP_FAIL;
    }
    return ESP_ERR_NOT_FOUND;
}

esp_err_t sensor_read_temperature(float *out_c)
{
    if (!out_c) return ESP_ERR_INVALID_ARG;
    if (detected == SENSOR_TYPE_BME280) {
        return bme280_app_read_temperature(out_c);
    } else if (detected == SENSOR_TYPE_SHT41) {
        return sht41_read_temperature(out_c);
    } else if (detected == SENSOR_TYPE_AHT20_BMP280) {
        // Read temperature from AHT20 (preferred) or BMP280 as fallback
        esp_err_t ret = aht20_read_temperature(out_c);
        if (ret == ESP_OK) return ESP_OK;
        return bmp280_read_temperature(out_c);
    }
    return ESP_ERR_NOT_FOUND;
}

esp_err_t sensor_read_humidity(float *out_percent)
{
    if (!out_percent) return ESP_ERR_INVALID_ARG;
    if (detected == SENSOR_TYPE_BME280) {
        return bme280_app_read_humidity(out_percent);
    } else if (detected == SENSOR_TYPE_SHT41) {
        return sht41_read_humidity(out_percent);
    } else if (detected == SENSOR_TYPE_AHT20_BMP280) {
        return aht20_read_humidity(out_percent);
    }
    return ESP_ERR_NOT_FOUND;
}

esp_err_t sensor_read_pressure(float *out_hpa)
{
    if (!out_hpa) return ESP_ERR_INVALID_ARG;
    if (detected == SENSOR_TYPE_BME280) {
        return bme280_app_read_pressure(out_hpa);
    } else if (detected == SENSOR_TYPE_SHT41) {
        // SHT41 has no pressure sensor - return default value
        *out_hpa = DEFAULT_PRESSURE_HPA;
        return ESP_OK;
    } else if (detected == SENSOR_TYPE_AHT20_BMP280) {
        return bmp280_read_pressure(out_hpa);
    }
    return ESP_ERR_NOT_FOUND;
}
