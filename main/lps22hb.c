/*
 * LPS22HB Pressure Sensor Driver
 * MEMS barometric pressure + temperature sensor (STMicroelectronics)
 *
 * Operated in one-shot mode for low-power battery use: the device stays idle
 * until a measurement is triggered, then automatically returns to power-down.
 */

#include "lps22hb.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "LPS22HB";

/* LPS22HB Registers */
#define LPS22HB_REG_WHO_AM_I    0x0F    // Device ID (reads 0xB1)
#define LPS22HB_REG_CTRL_REG1   0x10    // Control register 1 (ODR, BDU)
#define LPS22HB_REG_CTRL_REG2   0x11    // Control register 2 (ONE_SHOT, reset)
#define LPS22HB_REG_STATUS      0x27    // Status (data available flags)
#define LPS22HB_REG_PRESS_OUT_XL 0x28   // Pressure output, low byte
#define LPS22HB_REG_PRESS_OUT_L  0x29
#define LPS22HB_REG_PRESS_OUT_H  0x2A   // Pressure output, high byte
#define LPS22HB_REG_TEMP_OUT_L   0x2B   // Temperature output, low byte
#define LPS22HB_REG_TEMP_OUT_H   0x2C   // Temperature output, high byte

#define LPS22HB_WHO_AM_I_VALUE  0xB1

/* CTRL_REG1 bits */
#define LPS22HB_CTRL1_BDU       0x02    // Block data update (avoid torn reads)
/* ODR[2:0] = 000 keeps the device in one-shot (power-down) mode */

/* CTRL_REG2 bits */
#define LPS22HB_CTRL2_ONE_SHOT  0x01    // Trigger a single measurement

/* STATUS bits */
#define LPS22HB_STATUS_T_DA     0x02    // Temperature data available
#define LPS22HB_STATUS_P_DA     0x01    // Pressure data available

/* Auto-increment bit for multi-byte reads */
#define LPS22HB_AUTO_INCREMENT  0x80

static i2c_bus_device_handle_t s_dev = NULL;

static esp_err_t lps22hb_read_reg(uint8_t reg, uint8_t *data, size_t len)
{
    /* Set MSB to auto-increment the register pointer on multi-byte reads */
    if (len > 1) reg |= LPS22HB_AUTO_INCREMENT;
    return i2c_bus_read_bytes(s_dev, reg, len, data);
}

static esp_err_t lps22hb_write_reg(uint8_t reg, uint8_t value)
{
    return i2c_bus_write_byte(s_dev, reg, value);
}

esp_err_t lps22hb_init(i2c_bus_handle_t i2c_bus)
{
    if (!i2c_bus) return ESP_ERR_INVALID_ARG;

    s_dev = i2c_bus_device_create(i2c_bus, LPS22HB_I2C_ADDR, 0);
    if (!s_dev) {
        ESP_LOGW(TAG, "lps22hb_init: device_create failed");
        return ESP_ERR_NOT_FOUND;
    }

    /* Verify device identity */
    uint8_t who = 0;
    esp_err_t ret = lps22hb_read_reg(LPS22HB_REG_WHO_AM_I, &who, 1);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "lps22hb_init: WHO_AM_I read failed");
        i2c_bus_device_delete(&s_dev);
        return ESP_ERR_NOT_FOUND;
    }
    if (who != LPS22HB_WHO_AM_I_VALUE) {
        ESP_LOGW(TAG, "lps22hb_init: unexpected WHO_AM_I 0x%02X (expected 0x%02X)", who, LPS22HB_WHO_AM_I_VALUE);
        i2c_bus_device_delete(&s_dev);
        return ESP_ERR_NOT_FOUND;
    }

    /* One-shot mode: ODR=000 (power-down between reads), block data update on */
    ret = lps22hb_write_reg(LPS22HB_REG_CTRL_REG1, LPS22HB_CTRL1_BDU);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "lps22hb_init: CTRL_REG1 write failed");
        i2c_bus_device_delete(&s_dev);
        return ESP_ERR_NOT_FOUND;
    }

    ESP_LOGI(TAG, "lps22hb_init: probe OK (one-shot mode)");
    return ESP_OK;
}

esp_err_t lps22hb_trigger_measurement(void)
{
    if (s_dev == NULL) return ESP_ERR_NOT_FOUND;

    /* Start a one-shot conversion */
    esp_err_t ret = lps22hb_write_reg(LPS22HB_REG_CTRL_REG2, LPS22HB_CTRL2_ONE_SHOT);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "lps22hb_trigger_measurement: ONE_SHOT write failed");
        return ret;
    }

    /* Poll STATUS until both pressure and temperature are available.
     * A one-shot conversion completes well within ~40ms. */
    for (int i = 0; i < 10; i++) {
        vTaskDelay(pdMS_TO_TICKS(5));
        uint8_t status = 0;
        if (lps22hb_read_reg(LPS22HB_REG_STATUS, &status, 1) == ESP_OK) {
            if ((status & (LPS22HB_STATUS_P_DA | LPS22HB_STATUS_T_DA)) ==
                (LPS22HB_STATUS_P_DA | LPS22HB_STATUS_T_DA)) {
                return ESP_OK;
            }
        }
    }

    ESP_LOGW(TAG, "lps22hb_trigger_measurement: data not ready (timeout)");
    return ESP_ERR_TIMEOUT;
}

esp_err_t lps22hb_read_pressure(float *pressure)
{
    if (!pressure) return ESP_ERR_INVALID_ARG;
    if (s_dev == NULL) return ESP_ERR_NOT_FOUND;

    uint8_t raw[3];
    esp_err_t ret = lps22hb_read_reg(LPS22HB_REG_PRESS_OUT_XL, raw, 3);
    if (ret != ESP_OK) return ret;

    /* 24-bit signed value, LSB first */
    int32_t p_raw = ((uint32_t)raw[2] << 16) | ((uint32_t)raw[1] << 8) | (uint32_t)raw[0];
    if (p_raw & 0x800000) p_raw -= 0x1000000;  // Sign extend

    /* LPS22HB: pressure[hPa] = raw / 4096 */
    *pressure = (float)p_raw / 4096.0f;
    return ESP_OK;
}

esp_err_t lps22hb_read_temperature(float *temperature)
{
    if (!temperature) return ESP_ERR_INVALID_ARG;
    if (s_dev == NULL) return ESP_ERR_NOT_FOUND;

    uint8_t raw[2];
    esp_err_t ret = lps22hb_read_reg(LPS22HB_REG_TEMP_OUT_L, raw, 2);
    if (ret != ESP_OK) return ret;

    /* 16-bit signed value, LSB first */
    int16_t t_raw = (int16_t)(((uint16_t)raw[1] << 8) | (uint16_t)raw[0]);

    /* LPS22HB: temperature[°C] = raw / 100 */
    *temperature = (float)t_raw / 100.0f;
    return ESP_OK;
}
