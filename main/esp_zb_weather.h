/*
 * SPDX-FileCopyrightText: 2021-2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier:  LicenseRef-Included
 *
 * Zigbee HA_on_off_light Example
 *
 * This example code is in the Public Domain (or CC0 licensed, at your option.)
 *
 * Unless required by applicable law or agreed to in writing, this
 * software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
 * CONDITIONS OF ANY KIND, either express or implied.
 */

#include "esp_zigbee_core.h"
#include "zcl_utility.h"

/* Zigbee configuration */
#define INSTALLCODE_POLICY_ENABLE       false                                /* enable the install code policy for security */
#define ED_AGING_TIMEOUT                ESP_ZB_ED_AGING_TIMEOUT_8MIN         /* aging timeout for sleepy end device */
#define ED_KEEP_ALIVE                   15000                                /* 15000 millisecond - poll parent every 15s */

/* Hardware v2.0 Endpoints */
#define HA_ESP_ENV_SENSOR_ENDPOINT      1                                    /* Environmental sensors (SHT4x + DPS368) via I2C Bus 1 */
#define HA_ESP_RAIN_GAUGE_ENDPOINT      2                                    /* Rain gauge sensor (reed switch) */
#define HA_ESP_DS18B20_ENDPOINT         3                                    /* DS18B20 temperature sensor (GPIO24) */
#define HA_ESP_WIND_SPEED_ENDPOINT      4                                    /* Anemometer (SS445P hall sensor) */
#define HA_ESP_WIND_DIR_ENDPOINT        5                                    /* Wind direction (AS5600 magnetic encoder) via I2C Bus 2 */
#define HA_ESP_LIGHT_ENDPOINT           6                                    /* Light sensor (VEML7700) via I2C Bus 2 */

#define ESP_ZB_PRIMARY_CHANNEL_MASK     ESP_ZB_TRANSCEIVER_ALL_CHANNELS_MASK /* Zigbee primary channel mask use in the example */

/* Debug LED configuration */
#define DEBUG_LED_ENABLE                1                                    /* Set to 1 to enable LED debug indicator */
#define DEBUG_LED_TYPE_RGB              1                                    /* Set to 1 for WS2812 RGB LED, 0 for simple GPIO */
#define DEBUG_LED_GPIO                  GPIO_NUM_8                           /* Built-in RGB LED on ESP32-H2 SuperMini */

/* GPIO Pin Configuration - Hardware v2.0 */
/* I2C Bus 1 - Environmental sensors */
#define I2C_BUS1_SDA_GPIO               GPIO_NUM_10                          /* I2C Bus 1 SDA - SHT4x + DPS368 */
#define I2C_BUS1_SCL_GPIO               GPIO_NUM_11                          /* I2C Bus 1 SCL - SHT4x + DPS368 */

/* I2C Bus 2 - Wind and light sensors */
#define I2C_BUS2_SDA_GPIO               GPIO_NUM_1                           /* I2C Bus 2 SDA - AS5600 + VEML7700 */
#define I2C_BUS2_SCL_GPIO               GPIO_NUM_2                           /* I2C Bus 2 SCL - AS5600 + VEML7700 */

/* Digital sensors */
#define RAIN_WAKE_GPIO                  GPIO_NUM_12                          /* Rain gauge reed switch (pulse counter) */
#define DS18B20_GPIO                    GPIO_NUM_24                          /* DS18B20 1-Wire temperature sensor */
#define ANEMOMETER_GPIO                 GPIO_NUM_14                          /* Anemometer SS445P hall sensor (pulse counter) */

/* Battery monitoring - Hardware v2.0 */
#define BATTERY_ENABLE_GPIO             GPIO_NUM_3                           /* P-MOSFET + N-MOSFET enable for battery measurement */
#define BATTERY_ADC_GPIO                GPIO_NUM_4                           /* ADC input for battery voltage (ADC1_CH4) */
#define BATTERY_ADC_CHANNEL             ADC_CHANNEL_4                        /* ADC channel */
#define BATTERY_VOLTAGE_DIVIDER_R1      100000                               /* Top resistor in voltage divider (100kΩ) - update if different in v2.0 */
#define BATTERY_VOLTAGE_DIVIDER_R2      100000                               /* Bottom resistor in voltage divider (100kΩ) - update if different in v2.0 */

/* Deep sleep configuration for battery operation */
#define SLEEP_DURATION_MINUTES          5                                    /* Wake up every 5 minutes for periodic reading */
#define SLEEP_DURATION_S                (SLEEP_DURATION_MINUTES * 60)
#define RAIN_MM_THRESHOLD               1.0f                                 /* Wake up immediately if rain > 1mm */

/* Basic manufacturer information - now using CMakeLists.txt definitions */
#define ESP_MANUFACTURER_NAME "\x09""ESPRESSIF"      /* Customized manufacturer name */
#define ESP_MODEL_IDENTIFIER "\x06""caelum"          /* Customized model identifier matching CMakeLists.txt project name */

/* Firmware version information */
#ifdef FW_VERSION
#define FIRMWARE_VERSION_STRING         FW_VERSION                           /* From CMakeLists.txt */
#else
#define FIRMWARE_VERSION_STRING         "0.1.0"                              /* Default version */
#endif

#define ESP_ZB_ZED_CONFIG()                                         \
    {                                                               \
        .esp_zb_role = ESP_ZB_DEVICE_TYPE_ED,                       \
        .install_code_policy = INSTALLCODE_POLICY_ENABLE,           \
        .nwk_cfg.zed_cfg = {                                        \
            .ed_timeout = ED_AGING_TIMEOUT,                         \
            .keep_alive = ED_KEEP_ALIVE,                            \
        },                                                          \
    }

#define ESP_ZB_DEFAULT_RADIO_CONFIG()                           \
    {                                                           \
        .radio_mode = ZB_RADIO_MODE_NATIVE,                     \
    }

#define ESP_ZB_DEFAULT_HOST_CONFIG()                            \
    {                                                           \
        .host_connection_mode = ZB_HOST_CONNECTION_MODE_NONE,   \
    }
