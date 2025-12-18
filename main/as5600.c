/*
 * AS5600 Magnetic Position Sensor Driver
 * 12-bit magnetic rotary position sensor for wind direction
 */

#include "as5600.h"
#include "esp_log.h"

static const char *TAG = "AS5600";

/* AS5600 Registers */
#define AS5600_REG_STATUS       0x0B    // Status register
#define AS5600_REG_RAW_ANGLE_H  0x0C    // Raw angle high byte
#define AS5600_REG_RAW_ANGLE_L  0x0D    // Raw angle low byte
#define AS5600_REG_ANGLE_H      0x0E    // Angle high byte (scaled)
#define AS5600_REG_ANGLE_L      0x0F    // Angle low byte (scaled)
#define AS5600_REG_AGC          0x1A    // Automatic gain control
#define AS5600_REG_MAGNITUDE_H  0x1B    // Magnitude high byte
#define AS5600_REG_MAGNITUDE_L  0x1C    // Magnitude low byte

/* Status register bits */
#define AS5600_STATUS_MH        (1 << 3)  // Magnet too strong
#define AS5600_STATUS_ML        (1 << 4)  // Magnet too weak
#define AS5600_STATUS_MD        (1 << 5)  // Magnet detected

static i2c_bus_device_handle_t as5600_dev = NULL;

/* Calibration offset for wind vane orientation (adjust during installation) */
#define WIND_DIRECTION_OFFSET_DEG  0.0f

/* Helper: read register */
static esp_err_t as5600_read_reg(uint8_t reg, uint8_t *data, size_t len)
{
    return i2c_bus_read_bytes(as5600_dev, reg, len, data);
}

esp_err_t as5600_init(i2c_bus_handle_t i2c_bus)
{
    if (!i2c_bus) return ESP_ERR_INVALID_ARG;

    /* Create I2C device handle */
    as5600_dev = i2c_bus_device_create(i2c_bus, AS5600_I2C_ADDR, 0);
    if (!as5600_dev) {
        ESP_LOGE(TAG, "Failed to create I2C device");
        return ESP_FAIL;
    }

    /* Check status register for magnet detection */
    uint8_t status;
    esp_err_t ret = as5600_read_reg(AS5600_REG_STATUS, &status, 1);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read status register");
        return ret;
    }

    /* Verify magnet is detected and field strength is good */
    if (!(status & AS5600_STATUS_MD)) {
        ESP_LOGW(TAG, "Magnet not detected (status: 0x%02X)", status);
    } else if (status & AS5600_STATUS_MH) {
        ESP_LOGW(TAG, "Magnet too strong (status: 0x%02X)", status);
    } else if (status & AS5600_STATUS_ML) {
        ESP_LOGW(TAG, "Magnet too weak (status: 0x%02X)", status);
    } else {
        ESP_LOGI(TAG, "Magnet detected, field strength OK");
    }

    ESP_LOGI(TAG, "AS5600 initialized");
    return ESP_OK;
}

esp_err_t as5600_read_angle_raw(uint16_t *angle)
{
    if (!angle || !as5600_dev) return ESP_ERR_INVALID_ARG;

    /* Read ANGLE registers (12-bit value in 0x0E-0x0F) */
    uint8_t data[2];
    esp_err_t ret = as5600_read_reg(AS5600_REG_ANGLE_H, data, 2);
    if (ret != ESP_OK) return ret;

    /* Combine high and low bytes (12-bit resolution: 0-4095) */
    *angle = ((uint16_t)data[0] << 8) | data[1];
    *angle &= 0x0FFF;  // Mask to 12 bits

    return ESP_OK;
}

esp_err_t as5600_read_angle_degrees(float *degrees)
{
    if (!degrees) return ESP_ERR_INVALID_ARG;

    uint16_t raw_angle;
    esp_err_t ret = as5600_read_angle_raw(&raw_angle);
    if (ret != ESP_OK) return ret;

    /* Convert 12-bit value (0-4095) to degrees (0-360) */
    *degrees = (raw_angle * 360.0f) / 4096.0f;

    return ESP_OK;
}

esp_err_t as5600_get_wind_direction(float *direction)
{
    if (!direction) return ESP_ERR_INVALID_ARG;

    float angle_deg;
    esp_err_t ret = as5600_read_angle_degrees(&angle_deg);
    if (ret != ESP_OK) return ret;

    /* Apply calibration offset for physical orientation */
    *direction = angle_deg + WIND_DIRECTION_OFFSET_DEG;
    
    /* Normalize to 0-360° */
    while (*direction < 0) *direction += 360.0f;
    while (*direction >= 360.0f) *direction -= 360.0f;

    return ESP_OK;
}

esp_err_t as5600_check_magnet(bool *detected)
{
    if (!detected || !as5600_dev) return ESP_ERR_INVALID_ARG;

    /* TODO: Check STATUS register (0x0B)
     * Bit 5: MD (Magnet Detected) - 1 = magnet detected
     * Bit 4: ML (Magnet Too Strong) - 1 = magnet too strong
     * Bit 3: MH (Magnet Too Weak) - 1 = magnet too weak
     */
    
    *detected = true;  // Placeholder
    return ESP_OK;
}
