#pragma once

#include "i2c_bus.h"
#include "esp_err.h"

// Initialize SHT41 on the provided I2C bus
esp_err_t sht41_init(i2c_bus_handle_t i2c_bus);

// Trigger a measurement
esp_err_t sht41_trigger_measurement(void);

// Read temperature in degrees Celsius
esp_err_t sht41_read_temperature(float *out_c);

// Read relative humidity in percent (0-100)
esp_err_t sht41_read_humidity(float *out_percent);
