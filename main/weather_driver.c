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

#include "esp_log.h"
#include "led_strip.h"
#include "weather_driver.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"


static led_strip_handle_t s_led_strip;
static uint8_t s_red = 255, s_green = 255, s_blue = 255;
static bool s_led_power_state = false;  // Track LED power state

void light_driver_set_power(bool power)
{
    s_led_power_state = power;  // Update state tracker
    ESP_ERROR_CHECK(led_strip_set_pixel(s_led_strip, 0, s_red * power, s_green * power, s_blue * power));
    ESP_ERROR_CHECK(led_strip_refresh(s_led_strip));
}

bool light_driver_get_power(void)
{
    return s_led_power_state;
}

void light_driver_set_gpio_power(bool power)
{
    gpio_set_level(CONFIG_EXAMPLE_GPIO_LED, power ? 1 : 0);
}

void light_driver_init(bool power)
{
    // Initialize LED strip
    led_strip_config_t led_strip_conf = {
        .max_leds = CONFIG_EXAMPLE_STRIP_LED_NUMBER,
        .strip_gpio_num = CONFIG_EXAMPLE_STRIP_LED_GPIO,
    };
    led_strip_rmt_config_t rmt_conf = {
        .resolution_hz = 10 * 1000 * 1000, // 10MHz
    };
    ESP_ERROR_CHECK(led_strip_new_rmt_device(&led_strip_conf, &rmt_conf, &s_led_strip));
    light_driver_set_power(power);
    
    // Initialize GPIO LED
    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = (1ULL << CONFIG_EXAMPLE_GPIO_LED),
        .pull_down_en = 0,
        .pull_up_en = 0,
    };
    gpio_config(&io_conf);
    light_driver_set_gpio_power(false);
    
    // Initialize Button GPIO (input with pull-down - external pull-down resistor)
    // Reset the GPIO to make sure it's not configured for other functions
    gpio_reset_pin(CONFIG_EXAMPLE_BUTTON_GPIO);
    
    gpio_config_t button_conf = {
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_INPUT,
        .pin_bit_mask = (1ULL << CONFIG_EXAMPLE_BUTTON_GPIO),
        .pull_down_en = 0,  // External pull-down resistor
        .pull_up_en = 0,
    };
    gpio_config(&button_conf);
}



/* Builtin button interrupt-based implementation (based on ESP-IDF switch example) */
static QueueHandle_t builtin_button_evt_queue = NULL;
static builtin_button_callback_t builtin_button_callback = NULL;
static const char *BUILTIN_BUTTON_TAG = "BUILTIN_BUTTON";

static void IRAM_ATTR builtin_button_isr_handler(void *arg)
{
    uint32_t gpio_num = CONFIG_EXAMPLE_BUILTIN_BUTTON_GPIO;
    xQueueSendFromISR(builtin_button_evt_queue, &gpio_num, NULL);
}

static void builtin_button_task(void *arg)
{
    uint32_t io_num;
    builtin_button_state_t button_state = BUILTIN_BUTTON_IDLE;
    bool evt_flag = false;
    TickType_t press_start_time = 0;
    bool long_press_reported = false;
    const TickType_t LONG_PRESS_DURATION = pdMS_TO_TICKS(5000); // 5 seconds for factory reset

    for (;;) {
        /* Wait for button interrupt */
        if (xQueueReceive(builtin_button_evt_queue, &io_num, portMAX_DELAY)) {
            /* Disable interrupts during debouncing */
            gpio_intr_disable(CONFIG_EXAMPLE_BUILTIN_BUTTON_GPIO);
            evt_flag = true;
        }

        while (evt_flag) {
            bool button_level = gpio_get_level(CONFIG_EXAMPLE_BUILTIN_BUTTON_GPIO);
            TickType_t current_time = xTaskGetTickCount();
            
            switch (button_state) {
            case BUILTIN_BUTTON_IDLE:
                if (button_level == 0) {
                    button_state = BUILTIN_BUTTON_PRESS_DETECTED;
                    press_start_time = current_time;
                    long_press_reported = false;
                    ESP_LOGI(BUILTIN_BUTTON_TAG, "Builtin button press detected");
                }
                break;
                
            case BUILTIN_BUTTON_PRESS_DETECTED:
                if (button_level == 0) {
                    /* Check if long press duration exceeded */
                    if ((current_time - press_start_time) >= LONG_PRESS_DURATION && !long_press_reported) {
                        ESP_LOGI(BUILTIN_BUTTON_TAG, "Builtin button long press detected (>5s) - Factory Reset");
                        if (builtin_button_callback) {
                            builtin_button_callback(BUTTON_ACTION_HOLD);
                        }
                        long_press_reported = true;
                    }
                    /* Button still pressed, stay in this state */
                } else {
                    /* Button released */
                    TickType_t press_duration = current_time - press_start_time;
                    button_state = BUILTIN_BUTTON_RELEASE_DETECTED;
                    
                    if (!long_press_reported) {
                        if (press_duration < LONG_PRESS_DURATION) {
                            ESP_LOGI(BUILTIN_BUTTON_TAG, "Builtin button short press");
                            if (builtin_button_callback) {
                                builtin_button_callback(BUTTON_ACTION_SINGLE);
                            }
                        }
                    } else {
                        ESP_LOGI(BUILTIN_BUTTON_TAG, "Builtin button released after long press");
                        if (builtin_button_callback) {
                            builtin_button_callback(BUTTON_ACTION_RELEASE_AFTER_HOLD);
                        }
                    }
                }
                break;
                
            case BUILTIN_BUTTON_RELEASE_DETECTED:
                button_state = BUILTIN_BUTTON_IDLE;
                break;
                
            default:
                break;
            }
            
            if (button_state == BUILTIN_BUTTON_IDLE) {
                /* Re-enable interrupts */
                gpio_intr_enable(CONFIG_EXAMPLE_BUILTIN_BUTTON_GPIO);
                evt_flag = false;
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(100)); // 100ms check interval
        }
    }
}

bool builtin_button_driver_init(builtin_button_callback_t callback)
{
    if (!callback) {
        ESP_LOGE(BUILTIN_BUTTON_TAG, "Callback function is NULL");
        return false;
    }

    builtin_button_callback = callback;

    /* Configure GPIO for builtin button */
    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_NEGEDGE,  // Interrupt on falling edge (button press)
        .pin_bit_mask = (1ULL << CONFIG_EXAMPLE_BUILTIN_BUTTON_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = 1,  // Enable internal pull-up for BOOT button
        .pull_down_en = 0,
    };
    esp_err_t ret = gpio_config(&io_conf);
    if (ret != ESP_OK) {
        ESP_LOGE(BUILTIN_BUTTON_TAG, "Failed to configure GPIO: %s", esp_err_to_name(ret));
        return false;
    }

    /* Create queue for GPIO events */
    builtin_button_evt_queue = xQueueCreate(10, sizeof(uint32_t));
    if (!builtin_button_evt_queue) {
        ESP_LOGE(BUILTIN_BUTTON_TAG, "Failed to create event queue");
        return false;
    }

    /* Install GPIO ISR service if not already installed */
    esp_err_t isr_ret = gpio_install_isr_service(ESP_INTR_FLAG_DEFAULT);
    if (isr_ret != ESP_OK && isr_ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(BUILTIN_BUTTON_TAG, "Failed to install ISR service: %s", esp_err_to_name(isr_ret));
        return false;
    }
    
    /* Add ISR handler for builtin button */
    ret = gpio_isr_handler_add(CONFIG_EXAMPLE_BUILTIN_BUTTON_GPIO, builtin_button_isr_handler, NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(BUILTIN_BUTTON_TAG, "Failed to add ISR handler: %s", esp_err_to_name(ret));
        return false;
    }

    /* Create button handling task */
    BaseType_t task_ret = xTaskCreate(builtin_button_task, "builtin_button", 2048, NULL, 10, NULL);
    if (task_ret != pdPASS) {
        ESP_LOGE(BUILTIN_BUTTON_TAG, "Failed to create button task");
        return false;
    }

    ESP_LOGI(BUILTIN_BUTTON_TAG, "Builtin button driver initialized successfully on GPIO%d", CONFIG_EXAMPLE_BUILTIN_BUTTON_GPIO);
    return true;
}

/* External button interrupt-based implementation */
static QueueHandle_t external_button_evt_queue = NULL;
static external_button_callback_t external_button_callback = NULL;
static const char *EXTERNAL_BUTTON_TAG = "EXTERNAL_BUTTON";

/* Button action timing constants (in milliseconds) */
#define BUTTON_DEBOUNCE_MS          50
#define BUTTON_DOUBLE_CLICK_MS      400
#define BUTTON_LONG_PRESS_MS        1000

static void IRAM_ATTR external_button_isr_handler(void *arg)
{
    uint32_t gpio_num = CONFIG_EXAMPLE_BUTTON_GPIO;
    xQueueSendFromISR(external_button_evt_queue, &gpio_num, NULL);
}

static void external_button_task(void *arg)
{
    uint32_t io_num;
    TickType_t press_start_time = 0;
    TickType_t first_click_time = 0;
    bool button_is_pressed = false;
    bool waiting_for_double_click = false;
    bool long_press_detected = false;
    uint8_t click_count = 0;
    
    for (;;) {
        TickType_t timeout = portMAX_DELAY;
        
        // If waiting for double click, calculate remaining timeout
        if (waiting_for_double_click) {
            TickType_t elapsed = xTaskGetTickCount() - first_click_time;
            TickType_t double_click_timeout = pdMS_TO_TICKS(BUTTON_DOUBLE_CLICK_MS);
            if (elapsed >= double_click_timeout) {
                timeout = 0; // Force immediate timeout handling
            } else {
                timeout = double_click_timeout - elapsed;
            }
        }
        
        // If button is pressed and we haven't detected long press yet, use shorter timeout to check for hold
        if (button_is_pressed && !long_press_detected) {
            TickType_t elapsed = xTaskGetTickCount() - press_start_time;
            TickType_t hold_check_timeout = pdMS_TO_TICKS(BUTTON_LONG_PRESS_MS);
            if (elapsed >= hold_check_timeout) {
                timeout = 0; // Force immediate hold detection
            } else {
                TickType_t remaining = hold_check_timeout - elapsed;
                if (timeout == portMAX_DELAY || remaining < timeout) {
                    timeout = remaining;
                }
            }
        }
        
        if (xQueueReceive(external_button_evt_queue, &io_num, timeout)) {
            gpio_intr_disable(CONFIG_EXAMPLE_BUTTON_GPIO);
            
            // Debounce
            vTaskDelay(pdMS_TO_TICKS(BUTTON_DEBOUNCE_MS));
            
            bool current_level = gpio_get_level(CONFIG_EXAMPLE_BUTTON_GPIO);
            bool is_pressed = (current_level == 1); // High when pressed (pull-down resistor)
            
            ESP_LOGD(EXTERNAL_BUTTON_TAG, "GPIO%d level: %d, is_pressed: %d, button_was_pressed: %d", 
                     CONFIG_EXAMPLE_BUTTON_GPIO, current_level, is_pressed, button_is_pressed);
            
            if (is_pressed && !button_is_pressed) {
                // Button press detected
                button_is_pressed = true;
                press_start_time = xTaskGetTickCount();
                long_press_detected = false;
                
            } else if (!is_pressed && button_is_pressed) {
                // Button release detected
                button_is_pressed = false;
                TickType_t press_end_time = xTaskGetTickCount();
                
                TickType_t press_duration = press_end_time - press_start_time;
                uint32_t press_duration_ms = (press_duration * 1000) / configTICK_RATE_HZ;
                
                if (long_press_detected) {
                    // Release after long press
                    if (external_button_callback) {
                        ESP_LOGI(EXTERNAL_BUTTON_TAG, "Action: Release after hold");
                        external_button_callback(BUTTON_ACTION_RELEASE_AFTER_HOLD);
                    }
                    waiting_for_double_click = false;
                    click_count = 0;
                } else if (press_duration_ms >= BUTTON_LONG_PRESS_MS) {
                    // This shouldn't happen as long press should be detected during press
                    // But handle it just in case
                    if (external_button_callback) {
                        ESP_LOGI(EXTERNAL_BUTTON_TAG, "Action: Single (long)");
                        external_button_callback(BUTTON_ACTION_SINGLE);
                    }
                    waiting_for_double_click = false;
                    click_count = 0;
                } else {
                    // Short press - could be single or first part of double
                    click_count++;
                    if (click_count == 1) {
                        waiting_for_double_click = true;
                        first_click_time = xTaskGetTickCount();
                        // Don't call callback yet, wait to see if there's a second click
                    } else if (click_count == 2) {
                        // Double click detected
                        if (external_button_callback) {
                            ESP_LOGI(EXTERNAL_BUTTON_TAG, "Action: Double click");
                            external_button_callback(BUTTON_ACTION_DOUBLE);
                        }
                        waiting_for_double_click = false;
                        click_count = 0;
                    }
                }
            }
            
            gpio_intr_enable(CONFIG_EXAMPLE_BUTTON_GPIO);
            
        } else {
            // Timeout occurred - handle pending actions
            if (waiting_for_double_click && click_count == 1) {
                // Single click timeout - no second click came
                waiting_for_double_click = false;
                click_count = 0;
                if (external_button_callback) {
                    ESP_LOGI(EXTERNAL_BUTTON_TAG, "Action: Single click");
                    external_button_callback(BUTTON_ACTION_SINGLE);
                }
            }
            
            // Check for long press during button hold
            if (button_is_pressed && !long_press_detected) {
                TickType_t current_time = xTaskGetTickCount();
                TickType_t hold_duration = current_time - press_start_time;
                uint32_t hold_duration_ms = (hold_duration * 1000) / configTICK_RATE_HZ;
                
                if (hold_duration_ms >= BUTTON_LONG_PRESS_MS) {
                    long_press_detected = true;
                    if (external_button_callback) {
                        ESP_LOGI(EXTERNAL_BUTTON_TAG, "Action: Long press (hold)");
                        external_button_callback(BUTTON_ACTION_HOLD);
                    }
                    waiting_for_double_click = false;
                    click_count = 0;
                }
            }
        }
    }
}

bool external_button_driver_init(external_button_callback_t callback)
{
    if (!callback) {
        ESP_LOGE(EXTERNAL_BUTTON_TAG, "Callback function is NULL");
        return false;
    }

    external_button_callback = callback;

    /* Configure GPIO for external button - NOTE: different from builtin button */
    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_ANYEDGE,  // Interrupt on both edges (press and release detection)
        .pin_bit_mask = (1ULL << CONFIG_EXAMPLE_BUTTON_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = 0,   // No internal pull-up (external pull-down resistor)
        .pull_down_en = 0, // No internal pull-down (external resistor)
    };
    esp_err_t ret = gpio_config(&io_conf);
    if (ret != ESP_OK) {
        ESP_LOGE(EXTERNAL_BUTTON_TAG, "Failed to configure GPIO: %s", esp_err_to_name(ret));
        return false;
    }

    /* Create queue for GPIO events */
    external_button_evt_queue = xQueueCreate(10, sizeof(uint32_t));
    if (!external_button_evt_queue) {
        ESP_LOGE(EXTERNAL_BUTTON_TAG, "Failed to create event queue");
        return false;
    }

    /* Install GPIO ISR service if not already installed (ignore if already installed) */
    esp_err_t isr_ret = gpio_install_isr_service(ESP_INTR_FLAG_DEFAULT);
    if (isr_ret != ESP_OK && isr_ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(EXTERNAL_BUTTON_TAG, "Failed to install ISR service: %s", esp_err_to_name(isr_ret));
        return false;
    }
    
    /* Add ISR handler for external button */
    ret = gpio_isr_handler_add(CONFIG_EXAMPLE_BUTTON_GPIO, external_button_isr_handler, NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(EXTERNAL_BUTTON_TAG, "Failed to add ISR handler: %s", esp_err_to_name(ret));
        return false;
    }

    /* Create button handling task */
    BaseType_t task_ret = xTaskCreate(external_button_task, "external_button", 2048, NULL, 10, NULL);
    if (task_ret != pdPASS) {
        ESP_LOGE(EXTERNAL_BUTTON_TAG, "Failed to create button task");
        return false;
    }

    ESP_LOGI(EXTERNAL_BUTTON_TAG, "External button driver initialized successfully on GPIO%d", CONFIG_EXAMPLE_BUTTON_GPIO);
    return true;
}
