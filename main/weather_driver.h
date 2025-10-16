/*
 * SPDX-FileCopyrightText: 2021-2023 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: LicenseRef-Included
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form, except as embedded into a Espressif Systems
 *    integrated circuit in a product or a software update for such product,
 *    must reproduce the above copyright notice, this list of conditions and
 *    the following disclaimer in the documentation and/or other materials
 *    provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its contributors
 *    may be used to endorse or promote products derived from this software without
 *    specific prior written permission.
 *
 * 4. Any software provided in binary form under this license must not be reverse
 *    engineered, decompiled, modified and/or disassembled.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* light intensity level */
#define LIGHT_DEFAULT_ON  1
#define LIGHT_DEFAULT_OFF 0

/* LED strip configuration */
#define CONFIG_EXAMPLE_STRIP_LED_GPIO   8
#define CONFIG_EXAMPLE_STRIP_LED_NUMBER 1

/* GPIO LED configuration */
#define CONFIG_EXAMPLE_GPIO_LED         0

/* Button configuration */
#define CONFIG_EXAMPLE_BUTTON_GPIO      12
#define CONFIG_EXAMPLE_BUILTIN_BUTTON_GPIO  9   /* ESP32-C6 builtin button (BOOT button) */

#define ESP_INTR_FLAG_DEFAULT 0
/**
* @brief Set light power (on/off) for LED strip.
*
* @param  power  The light power to be set
*/
void light_driver_set_power(bool power);

/**
* @brief Get current light power state for LED strip.
*
* @return true if LED is on, false if LED is off
*/
bool light_driver_get_power(void);

/**
* @brief Set light power (on/off) for GPIO LED.
*
* @param  power  The light power to be set
*/
void light_driver_set_gpio_power(bool power);

/**
* @brief color light driver init, be invoked where you want to use color light
*
* @param power power on/off
*/
void light_driver_init(bool power);



/* Builtin button state machine */
typedef enum {
    BUILTIN_BUTTON_IDLE,
    BUILTIN_BUTTON_PRESS_DETECTED,
    BUILTIN_BUTTON_RELEASE_DETECTED,
} builtin_button_state_t;

/* Button action types */
typedef enum {
    BUTTON_ACTION_NONE,
    BUTTON_ACTION_SINGLE,
    BUTTON_ACTION_DOUBLE, 
    BUTTON_ACTION_HOLD,
    BUTTON_ACTION_RELEASE_AFTER_HOLD
} button_action_t;

/* Button callback function types */
typedef void (*builtin_button_callback_t)(button_action_t action);
typedef void (*external_button_callback_t)(button_action_t action);

/**
* @brief Initialize builtin button with interrupt-based handling
*
* @param callback Function to call when button is pressed and released
* @return true if initialization successful, false otherwise
*/
bool builtin_button_driver_init(builtin_button_callback_t callback);

/**
* @brief Initialize external button with interrupt-based handling
*
* @param callback Function to call when button state changes (with new state)
* @return true if initialization successful, false otherwise
*/
bool external_button_driver_init(external_button_callback_t callback);

#ifdef __cplusplus
} // extern "C"
#endif
