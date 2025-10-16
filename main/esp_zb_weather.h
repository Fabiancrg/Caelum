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
#include "weather_driver.h"
#include "zcl_utility.h"

/* Zigbee configuration */
#define INSTALLCODE_POLICY_ENABLE       false                                /* enable the install code policy for security */
#define ED_AGING_TIMEOUT                ESP_ZB_ED_AGING_TIMEOUT_8MIN         /* aging timeout for sleepy end device */
#define ED_KEEP_ALIVE                   15000                                /* 15000 millisecond - poll parent every 15s */
#define HA_ESP_LIGHT1_ENDPOINT          1                                    /* esp light bulb device endpoint, used to process light controlling commands */
#define HA_ESP_LIGHT2_ENDPOINT          2                                    /* esp light bulb device endpoint 2, used to process light controlling commands */
#define HA_ESP_BUTTON_ENDPOINT          3                                    /* esp button sensor endpoint */
#define HA_ESP_BME280_ENDPOINT          4                                    /* esp BME280 environmental sensor endpoint */
#define HA_ESP_RAIN_GAUGE_ENDPOINT      5                                    /* esp rain gauge sensor endpoint */
#define ESP_ZB_PRIMARY_CHANNEL_MASK     ESP_ZB_TRANSCEIVER_ALL_CHANNELS_MASK /* Zigbee primary channel mask use in the example */

/* Deep sleep configuration for battery operation */
#define SLEEP_DURATION_MINUTES          15                                   /* Wake up every 15 minutes for periodic reporting */
#define SLEEP_DURATION_S                (SLEEP_DURATION_MINUTES * 60)
#define RAIN_WAKE_GPIO                  GPIO_NUM_18                          /* GPIO for rain gauge wake-up */
#define RAIN_MM_THRESHOLD               1.0f                                 /* Wake up immediately if rain > 1mm */

/* Basic manufacturer information */
#define ESP_MANUFACTURER_NAME "\x09""ESPRESSIF"      /* Customized manufacturer name */
#define ESP_MODEL_IDENTIFIER "\x07"CONFIG_IDF_TARGET /* Customized model identifier */

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
