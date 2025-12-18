/*
 * DPS368 Pressure Sensor Driver
 * High-precision barometric pressure sensor
 */

#include "dps368.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "DPS368";

/* DPS368 Registers */
#define DPS368_REG_PSR_B2       0x00    // Pressure MSB
#define DPS368_REG_PSR_B1       0x01
#define DPS368_REG_PSR_B0       0x02    // Pressure LSB
#define DPS368_REG_TMP_B2       0x03    // Temperature MSB
#define DPS368_REG_TMP_B1       0x04
#define DPS368_REG_TMP_B0       0x05    // Temperature LSB
#define DPS368_REG_PRS_CFG      0x06    // Pressure config
#define DPS368_REG_TMP_CFG      0x07    // Temperature config
#define DPS368_REG_MEAS_CFG     0x08    // Measurement config
#define DPS368_REG_CFG_REG      0x09    // Configuration
#define DPS368_REG_PROD_ID      0x0D    // Product ID (should read 0x10)
#define DPS368_REG_COEF         0x10    // Calibration coefficients start

/* Measurement rates and oversampling */
#define DPS368_PM_RATE_1        0x00    // 1 measurement/sec
#define DPS368_PM_RATE_2        0x10    // 2 measurements/sec
#define DPS368_PM_RATE_4        0x20    // 4 measurements/sec
#define DPS368_PM_RATE_8        0x30    // 8 measurements/sec
#define DPS368_PM_PRC_1         0x00    // Oversample x1
#define DPS368_PM_PRC_2         0x01    // Oversample x2
#define DPS368_PM_PRC_4         0x02    // Oversample x4
#define DPS368_PM_PRC_8         0x03    // Oversample x8

#define DPS368_TMP_RATE_1       0x00    // 1 measurement/sec
#define DPS368_TMP_PRC_1        0x00    // Oversample x1

/* Measurement modes */
#define DPS368_MEAS_IDLE        0x00
#define DPS368_MEAS_PRESSURE    0x01    // Single pressure
#define DPS368_MEAS_TEMP        0x02    // Single temperature
#define DPS368_MEAS_CONT_PRES   0x05    // Continuous pressure
#define DPS368_MEAS_CONT_TEMP   0x06    // Continuous temperature
#define DPS368_MEAS_CONT_BOTH   0x07    // Continuous pressure + temperature

static i2c_bus_device_handle_t dps368_dev = NULL;

/* Calibration coefficients */
static int32_t c0, c1, c00, c10, c01, c11, c20, c21, c30;
static bool calibrated = false;

/* Helper: read register */
static esp_err_t dps368_read_reg(uint8_t reg, uint8_t *data, size_t len)
{
    return i2c_bus_read_bytes(dps368_dev, reg, len, data);
}

/* Helper: write register */
static esp_err_t dps368_write_reg(uint8_t reg, uint8_t value)
{
    return i2c_bus_write_byte(dps368_dev, reg, value);
}

/* Read and parse calibration coefficients */
static esp_err_t dps368_read_calibration(void)
{
    uint8_t coef[18];
    esp_err_t ret = dps368_read_reg(DPS368_REG_COEF, coef, 18);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read calibration coefficients");
        return ret;
    }

    /* Parse coefficients according to datasheet */
    c0 = ((uint32_t)coef[0] << 4) | (((uint32_t)coef[1] >> 4) & 0x0F);
    if (c0 & 0x800) c0 -= 0x1000;  // Sign extend 12-bit to 32-bit

    c1 = (((uint32_t)coef[1] & 0x0F) << 8) | (uint32_t)coef[2];
    if (c1 & 0x800) c1 -= 0x1000;

    c00 = ((uint32_t)coef[3] << 12) | ((uint32_t)coef[4] << 4) | (((uint32_t)coef[5] >> 4) & 0x0F);
    if (c00 & 0x80000) c00 -= 0x100000;  // Sign extend 20-bit

    c10 = (((uint32_t)coef[5] & 0x0F) << 16) | ((uint32_t)coef[6] << 8) | (uint32_t)coef[7];
    if (c10 & 0x80000) c10 -= 0x100000;

    c01 = ((uint32_t)coef[8] << 8) | (uint32_t)coef[9];
    if (c01 & 0x8000) c01 -= 0x10000;  // Sign extend 16-bit

    c11 = ((uint32_t)coef[10] << 8) | (uint32_t)coef[11];
    if (c11 & 0x8000) c11 -= 0x10000;

    c20 = ((uint32_t)coef[12] << 8) | (uint32_t)coef[13];
    if (c20 & 0x8000) c20 -= 0x10000;

    c21 = ((uint32_t)coef[14] << 8) | (uint32_t)coef[15];
    if (c21 & 0x8000) c21 -= 0x10000;

    c30 = ((uint32_t)coef[16] << 8) | (uint32_t)coef[17];
    if (c30 & 0x8000) c30 -= 0x10000;

    calibrated = true;
    ESP_LOGI(TAG, "Calibration coefficients loaded");
    return ESP_OK;
}

esp_err_t dps368_init(i2c_bus_handle_t i2c_bus)
{
    if (!i2c_bus) return ESP_ERR_INVALID_ARG;

    /* Create I2C device handle */
    dps368_dev = i2c_bus_device_create(i2c_bus, DPS368_I2C_ADDR, 0);
    if (!dps368_dev) {
        ESP_LOGE(TAG, "Failed to create I2C device");
        return ESP_FAIL;
    }

    /* Verify product ID */
    uint8_t prod_id;
    esp_err_t ret = dps368_read_reg(DPS368_REG_PROD_ID, &prod_id, 1);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read product ID");
        return ret;
    }

    if (prod_id != 0x10) {
        ESP_LOGE(TAG, "Invalid product ID: 0x%02X (expected 0x10)", prod_id);
        return ESP_FAIL;
    }

    /* Read calibration coefficients */
    ret = dps368_read_calibration();
    if (ret != ESP_OK) return ret;

    /* Configure pressure measurement: 8 measurements/sec, oversample x8 */
    ret = dps368_write_reg(DPS368_REG_PRS_CFG, DPS368_PM_RATE_8 | DPS368_PM_PRC_8);
    if (ret != ESP_OK) return ret;

    /* Configure temperature measurement: 1 measurement/sec, oversample x1 */
    ret = dps368_write_reg(DPS368_REG_TMP_CFG, DPS368_TMP_RATE_1 | DPS368_TMP_PRC_1);
    if (ret != ESP_OK) return ret;

    /* Set measurement mode to continuous pressure + temperature */
    ret = dps368_write_reg(DPS368_REG_MEAS_CFG, DPS368_MEAS_CONT_BOTH);
    if (ret != ESP_OK) return ret;

    ESP_LOGI(TAG, "DPS368 initialized (continuous mode, 8x oversample)");
    return ESP_OK;
}

esp_err_t dps368_read_temperature(float *temperature)
{
    if (!temperature || !dps368_dev) return ESP_ERR_INVALID_ARG;
    if (!calibrated) return ESP_ERR_INVALID_STATE;

    /* Read 24-bit temperature value */
    uint8_t data[3];
    esp_err_t ret = dps368_read_reg(DPS368_REG_TMP_B2, data, 3);
    if (ret != ESP_OK) return ret;

    /* Combine bytes into signed 24-bit value */
    int32_t temp_raw = ((uint32_t)data[0] << 16) | ((uint32_t)data[1] << 8) | (uint32_t)data[2];
    if (temp_raw & 0x800000) temp_raw -= 0x1000000;  // Sign extend

    /* Scale according to oversampling */
    float temp_scaled = (float)temp_raw / 524288.0f;  // For oversample x1

    /* Apply calibration */
    *temperature = c0 * 0.5f + c1 * temp_scaled;

    return ESP_OK;
}

esp_err_t dps368_read_pressure(float *pressure)
{
    if (!pressure || !dps368_dev) return ESP_ERR_INVALID_ARG;
    if (!calibrated) return ESP_ERR_INVALID_STATE;

    /* Read 24-bit pressure value */
    uint8_t prs_data[3];
    esp_err_t ret = dps368_read_reg(DPS368_REG_PSR_B2, prs_data, 3);
    if (ret != ESP_OK) return ret;

    /* Read temperature for compensation */
    uint8_t tmp_data[3];
    ret = dps368_read_reg(DPS368_REG_TMP_B2, tmp_data, 3);
    if (ret != ESP_OK) return ret;

    /* Combine bytes into signed 24-bit values */
    int32_t prs_raw = ((uint32_t)prs_data[0] << 16) | ((uint32_t)prs_data[1] << 8) | (uint32_t)prs_data[2];
    if (prs_raw & 0x800000) prs_raw -= 0x1000000;

    int32_t tmp_raw = ((uint32_t)tmp_data[0] << 16) | ((uint32_t)tmp_data[1] << 8) | (uint32_t)tmp_data[2];
    if (tmp_raw & 0x800000) tmp_raw -= 0x1000000;

    /* Scale according to oversampling */
    float prs_scaled = (float)prs_raw / 253952.0f;  // For oversample x8
    float tmp_scaled = (float)tmp_raw / 524288.0f;  // For oversample x1

    /* Apply temperature-compensated pressure calculation */
    float prs_comp = c00 + prs_scaled * (c10 + prs_scaled * (c20 + prs_scaled * c30))
                     + tmp_scaled * c01 + tmp_scaled * prs_scaled * (c11 + prs_scaled * c21);

    *pressure = prs_comp / 100.0f;  // Convert Pa to hPa

    return ESP_OK;
}

esp_err_t dps368_trigger_measurement(void)
{
    /* In continuous mode, no trigger needed */
    return ESP_OK;
}
