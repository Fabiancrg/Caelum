/*
 * VEML7700 Ambient Light Sensor Driver
 * High accuracy ambient light sensor with 16-bit resolution
 */

#include "veml7700.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "VEML7700";

/* VEML7700 Registers */
#define VEML7700_REG_ALS_CONF   0x00    // Configuration register
#define VEML7700_REG_ALS_WH     0x01    // High threshold window
#define VEML7700_REG_ALS_WL     0x02    // Low threshold window
#define VEML7700_REG_PWR_SAVE   0x03    // Power saving mode
#define VEML7700_REG_ALS        0x04    // ALS data
#define VEML7700_REG_WHITE      0x05    // White channel data
#define VEML7700_REG_INT        0x06    // Interrupt status

/* Configuration register bits */
#define VEML7700_ALS_GAIN_1     0x0000  // Gain x1
#define VEML7700_ALS_GAIN_2     0x0400  // Gain x2
#define VEML7700_ALS_GAIN_1_8   0x0800  // Gain x1/8
#define VEML7700_ALS_GAIN_1_4   0x0C00  // Gain x1/4

#define VEML7700_ALS_IT_25MS    0x0300  // Integration time 25ms
#define VEML7700_ALS_IT_50MS    0x0200  // Integration time 50ms
#define VEML7700_ALS_IT_100MS   0x0000  // Integration time 100ms (default)
#define VEML7700_ALS_IT_200MS   0x0040  // Integration time 200ms
#define VEML7700_ALS_IT_400MS   0x0080  // Integration time 400ms
#define VEML7700_ALS_IT_800MS   0x00C0  // Integration time 800ms

#define VEML7700_ALS_PERS_1     0x0000  // Persistence protect 1
#define VEML7700_ALS_PERS_2     0x0010  // Persistence protect 2
#define VEML7700_ALS_PERS_4     0x0020  // Persistence protect 4
#define VEML7700_ALS_PERS_8     0x0030  // Persistence protect 8

#define VEML7700_ALS_INT_EN     0x0002  // Enable interrupt
#define VEML7700_ALS_SD         0x0001  // Shut down (power off)

static i2c_bus_device_handle_t veml7700_dev = NULL;

/* Current configuration */
static float current_resolution = 0.0036f;  // lux/count (depends on gain/integration time)

/* Helper: write 16-bit register */
static esp_err_t veml7700_write_reg(uint8_t reg, uint16_t value)
{
    uint8_t data[2];
    data[0] = value & 0xFF;        // LSB first
    data[1] = (value >> 8) & 0xFF; // MSB
    return i2c_bus_write_bytes(veml7700_dev, reg, 2, data);
}

/* Helper: read 16-bit register */
static esp_err_t veml7700_read_reg(uint8_t reg, uint16_t *value)
{
    uint8_t data[2];
    esp_err_t ret = i2c_bus_read_bytes(veml7700_dev, reg, 2, data);
    if (ret == ESP_OK) {
        *value = data[0] | (data[1] << 8);  // LSB first
    }
    return ret;
}

esp_err_t veml7700_init(i2c_bus_handle_t i2c_bus)
{
    if (!i2c_bus) return ESP_ERR_INVALID_ARG;

    /* Create I2C device handle */
    veml7700_dev = i2c_bus_device_create(i2c_bus, VEML7700_I2C_ADDR, 0);
    if (!veml7700_dev) {
        ESP_LOGE(TAG, "Failed to create I2C device");
        return ESP_FAIL;
    }

    /* Configure sensor: Gain x1, Integration time 100ms, Enable */
    uint16_t config = VEML7700_ALS_GAIN_1 | VEML7700_ALS_IT_100MS | VEML7700_ALS_PERS_1;
    esp_err_t ret = veml7700_write_reg(VEML7700_REG_ALS_CONF, config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to write configuration");
        return ret;
    }

    /* Set resolution based on gain x1 and integration time 100ms */
    current_resolution = 0.0036f;  // lux/count

    /* Wait for first measurement to complete (100ms integration time) */
    vTaskDelay(pdMS_TO_TICKS(150));

    ESP_LOGI(TAG, "VEML7700 initialized (Gain x1, IT 100ms, resolution %.4f lux/count)", 
             current_resolution);
    return ESP_OK;
}

esp_err_t veml7700_read_als_raw(uint16_t *als_raw)
{
    if (!als_raw || !veml7700_dev) return ESP_ERR_INVALID_ARG;

    /* Read ALS data register (0x04) */
    return veml7700_read_reg(VEML7700_REG_ALS, als_raw);
}

esp_err_t veml7700_read_lux(float *lux)
{
    if (!lux) return ESP_ERR_INVALID_ARG;

    uint16_t als_raw;
    esp_err_t ret = veml7700_read_als_raw(&als_raw);
    if (ret != ESP_OK) return ret;

    /* Convert raw count to lux using current resolution */
    *lux = als_raw * current_resolution;

    ESP_LOGD(TAG, "Light: %.2f lux (raw: %d)", *lux, als_raw);
    return ESP_OK;
}

esp_err_t veml7700_power_down(void)
{
    if (!veml7700_dev) return ESP_ERR_INVALID_STATE;

    /* Read current config */
    uint16_t config;
    esp_err_t ret = veml7700_read_reg(VEML7700_REG_ALS_CONF, &config);
    if (ret != ESP_OK) return ret;

    /* Set ALS_SD bit to shut down sensor */
    config |= VEML7700_ALS_SD;
    ret = veml7700_write_reg(VEML7700_REG_ALS_CONF, config);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "VEML7700 powered down");
    }
    return ret;
}

esp_err_t veml7700_power_up(void)
{
    if (!veml7700_dev) return ESP_ERR_INVALID_STATE;

    /* Read current config */
    uint16_t config;
    esp_err_t ret = veml7700_read_reg(VEML7700_REG_ALS_CONF, &config);
    if (ret != ESP_OK) return ret;

    /* Clear ALS_SD bit to wake up sensor */
    config &= ~VEML7700_ALS_SD;
    ret = veml7700_write_reg(VEML7700_REG_ALS_CONF, config);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "VEML7700 powered up");
        vTaskDelay(pdMS_TO_TICKS(5));  // Wait for sensor to stabilize
    }
    return ret;
}
