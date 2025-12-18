/*
 * I2C Bus Configuration for Hardware v2.0
 * Two independent I2C buses for sensor communication
 */

#include "i2c_config.h"
#include "esp_zb_weather.h"
#include "esp_log.h"

static const char *TAG = "I2C_CONFIG";

/* I2C Bus handles */
i2c_bus_handle_t i2c_bus1 = NULL;
i2c_bus_handle_t i2c_bus2 = NULL;

esp_err_t i2c_buses_init(void)
{
    /* Configure I2C Bus 1 - Environmental sensors (SHT4x + DPS368) */
    i2c_config_t i2c1_conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = I2C_BUS1_SDA_GPIO,
        .scl_io_num = I2C_BUS1_SCL_GPIO,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = 100000,  // 100 kHz for compatibility
    };
    
    i2c_bus1 = i2c_bus_create(I2C_NUM_0, &i2c1_conf);
    if (i2c_bus1 == NULL) {
        ESP_LOGE(TAG, "Failed to create I2C Bus 1");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "I2C Bus 1 initialized (GPIO%d/GPIO%d) - SHT4x + DPS368", 
             I2C_BUS1_SDA_GPIO, I2C_BUS1_SCL_GPIO);

    /* Configure I2C Bus 2 - Wind & Light sensors (AS5600 + VEML7700) */
    i2c_config_t i2c2_conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = I2C_BUS2_SDA_GPIO,
        .scl_io_num = I2C_BUS2_SCL_GPIO,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = 100000,  // 100 kHz
    };
    
    i2c_bus2 = i2c_bus_create(I2C_NUM_1, &i2c2_conf);
    if (i2c_bus2 == NULL) {
        ESP_LOGE(TAG, "Failed to create I2C Bus 2");
        i2c_bus_delete(&i2c_bus1);  // Clean up bus 1
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "I2C Bus 2 initialized (GPIO%d/GPIO%d) - AS5600 + VEML7700", 
             I2C_BUS2_SDA_GPIO, I2C_BUS2_SCL_GPIO);

    return ESP_OK;
}

esp_err_t i2c_buses_deinit(void)
{
    esp_err_t ret = ESP_OK;
    
    if (i2c_bus1 != NULL) {
        if (i2c_bus_delete(&i2c_bus1) != ESP_OK) {
            ESP_LOGE(TAG, "Failed to delete I2C Bus 1");
            ret = ESP_FAIL;
        }
    }
    
    if (i2c_bus2 != NULL) {
        if (i2c_bus_delete(&i2c_bus2) != ESP_OK) {
            ESP_LOGE(TAG, "Failed to delete I2C Bus 2");
            ret = ESP_FAIL;
        }
    }
    
    return ret;
}

i2c_bus_handle_t i2c_get_bus1(void)
{
    return i2c_bus1;
}

i2c_bus_handle_t i2c_get_bus2(void)
{
    return i2c_bus2;
}
