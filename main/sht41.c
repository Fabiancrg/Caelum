#include "sht41.h"
#include "i2c_bus.h"
#include "esp_log.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdint.h>

static const char *TAG = "SHT41";

// Default I2C address for SHT41
#define SHT41_I2C_ADDR 0x44

// Commands for SHT41
#define SHT41_CMD_MEASURE_HIGH_PRECISION 0xFD  // High precision measurement (~8.3ms)
#define SHT41_CMD_SOFT_RESET 0x94

static i2c_bus_device_handle_t s_dev = NULL;
static float s_last_temperature = 0.0f;
static float s_last_humidity = 0.0f;

// CRC-8 calculation for SHT41 (polynomial: 0x31, init: 0xFF)
static uint8_t sht41_crc8(const uint8_t *data, size_t len)
{
    uint8_t crc = 0xFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (uint8_t bit = 0; bit < 8; bit++) {
            if (crc & 0x80) {
                crc = (crc << 1) ^ 0x31;
            } else {
                crc = (crc << 1);
            }
        }
    }
    return crc;
}

esp_err_t sht41_init(i2c_bus_handle_t i2c_bus)
{
    if (!i2c_bus) return ESP_ERR_INVALID_ARG;

    // Create device handle
    i2c_bus_device_handle_t dev = i2c_bus_device_create(i2c_bus, SHT41_I2C_ADDR, 0);
    if (dev == NULL) {
        ESP_LOGW(TAG, "sht41_init: device_create failed");
        return ESP_ERR_NOT_FOUND;
    }

    // Send soft reset command
    uint8_t reset_cmd = SHT41_CMD_SOFT_RESET;
    esp_err_t ret = i2c_bus_write_bytes(dev, NULL_I2C_MEM_ADDR, 1, &reset_cmd);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "sht41_init: soft reset failed");
        i2c_bus_device_delete(&dev);
        return ESP_ERR_NOT_FOUND;
    }

    // Wait for reset to complete (typical 1ms)
    vTaskDelay(pdMS_TO_TICKS(2));

    // Try to trigger a measurement to confirm device presence
    uint8_t measure_cmd = SHT41_CMD_MEASURE_HIGH_PRECISION;
    ret = i2c_bus_write_bytes(dev, NULL_I2C_MEM_ADDR, 1, &measure_cmd);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "sht41_init: probe measurement failed");
        i2c_bus_device_delete(&dev);
        return ESP_ERR_NOT_FOUND;
    }

    ESP_LOGI(TAG, "sht41_init: probe OK");
    s_dev = dev;
    return ESP_OK;
}

esp_err_t sht41_trigger_measurement(void)
{
    if (s_dev == NULL) return ESP_ERR_NOT_FOUND;

    // Send high precision measurement command
    uint8_t cmd = SHT41_CMD_MEASURE_HIGH_PRECISION;
    esp_err_t ret = i2c_bus_write_bytes(s_dev, NULL_I2C_MEM_ADDR, 1, &cmd);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "sht41_trigger_measurement: write failed");
        return ret;
    }

    // Wait for measurement to complete (typical 8.3ms for high precision)
    vTaskDelay(pdMS_TO_TICKS(10));

    // Read 6 bytes: temp_msb, temp_lsb, temp_crc, hum_msb, hum_lsb, hum_crc
    uint8_t raw[6];
    ret = i2c_bus_read_bytes(s_dev, NULL_I2C_MEM_ADDR, 6, raw);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "sht41_trigger_measurement: read failed");
        return ret;
    }

    // Verify CRC for temperature
    uint8_t temp_crc = sht41_crc8(&raw[0], 2);
    if (temp_crc != raw[2]) {
        ESP_LOGW(TAG, "sht41: temperature CRC mismatch (expected 0x%02X, got 0x%02X)", temp_crc, raw[2]);
        return ESP_ERR_INVALID_CRC;
    }

    // Verify CRC for humidity
    uint8_t hum_crc = sht41_crc8(&raw[3], 2);
    if (hum_crc != raw[5]) {
        ESP_LOGW(TAG, "sht41: humidity CRC mismatch (expected 0x%02X, got 0x%02X)", hum_crc, raw[5]);
        return ESP_ERR_INVALID_CRC;
    }

    // Convert temperature: T = -45 + 175 * (raw / 65535)
    uint16_t temp_raw = (raw[0] << 8) | raw[1];
    s_last_temperature = -45.0f + 175.0f * ((float)temp_raw / 65535.0f);

    // Convert humidity: RH = -6 + 125 * (raw / 65535)
    uint16_t hum_raw = (raw[3] << 8) | raw[4];
    s_last_humidity = -6.0f + 125.0f * ((float)hum_raw / 65535.0f);
    
    // Clamp humidity to valid range (0-100%)
    if (s_last_humidity < 0.0f) s_last_humidity = 0.0f;
    if (s_last_humidity > 100.0f) s_last_humidity = 100.0f;

    ESP_LOGD(TAG, "SHT41 measurement: T=%.2f°C, RH=%.2f%%", s_last_temperature, s_last_humidity);

    return ESP_OK;
}

esp_err_t sht41_read_temperature(float *out_c)
{
    if (!out_c) return ESP_ERR_INVALID_ARG;
    if (s_dev == NULL) return ESP_ERR_NOT_FOUND;

    *out_c = s_last_temperature;
    return ESP_OK;
}

esp_err_t sht41_read_humidity(float *out_percent)
{
    if (!out_percent) return ESP_ERR_INVALID_ARG;
    if (s_dev == NULL) return ESP_ERR_NOT_FOUND;

    *out_percent = s_last_humidity;
    return ESP_OK;
}
