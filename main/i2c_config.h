/*
 * I2C Bus Configuration for Hardware v2.0
 * Two independent I2C buses for sensor communication
 */

#pragma once

#include "i2c_bus.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* I2C Bus handles */
extern i2c_bus_handle_t i2c_bus1;  // Environmental sensors: SHT4x + DPS368
extern i2c_bus_handle_t i2c_bus2;  // Wind & Light sensors: AS5600 + VEML7700

/**
 * @brief Initialize both I2C buses for hardware v2.0
 * 
 * Bus 1 (GPIO10/11): SHT4x temperature/humidity + DPS368 pressure
 * Bus 2 (GPIO1/2): AS5600 wind direction + VEML7700 light sensor
 * 
 * @return ESP_OK on success, ESP_FAIL otherwise
 */
esp_err_t i2c_buses_init(void);

/**
 * @brief Deinitialize both I2C buses
 * 
 * @return ESP_OK on success
 */
esp_err_t i2c_buses_deinit(void);

/**
 * @brief Get I2C Bus 1 handle (Environmental sensors)
 * 
 * @return i2c_bus_handle_t Bus 1 handle
 */
i2c_bus_handle_t i2c_get_bus1(void);

/**
 * @brief Get I2C Bus 2 handle (Wind & Light sensors)
 * 
 * @return i2c_bus_handle_t Bus 2 handle
 */
i2c_bus_handle_t i2c_get_bus2(void);

#ifdef __cplusplus
}
#endif
