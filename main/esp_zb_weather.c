/*
 * SPDX-FileCopyrightText: 2021-2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier:  LicenseRef-Included
 *
 * ESP32-H2 Zigbee Weather Station with Deep Sleep
 *
 * Battery-powered weather station with 15-minute wake intervals
 * and immediate wake-up on rain detection (>1mm)
 */
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_check.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "ha/esp_zigbee_ha_standard.h"
#include "esp_zb_weather.h"
#include "esp_zb_ota.h"
#include "sleep_manager.h"
#include "driver/gpio.h"
#include "bme280_app.h"
#include "i2c_bus.h"
#include "nvs.h"
#include "weather_driver.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_ota_ops.h"
#include "esp_app_format.h"
#include "esp_timer.h"
/* Generated header with FW_VERSION / FW_DATE_CODE - created at configure time */
#include "version.h"

/* OTA upgrade running versions */
#ifndef OTA_UPGRADE_RUNNING_FILE_VERSION
#define OTA_UPGRADE_RUNNING_FILE_VERSION                      0x00000000U
#endif
#ifndef OTA_UPGRADE_RUNNING_STACK_VERSION
#define OTA_UPGRADE_RUNNING_STACK_VERSION                     0x0002U
#endif

#if !defined ZB_ED_ROLE
#error Define ZB_ED_ROLE in idf.py menuconfig to compile Weather Station (End Device) source code.
#endif

static const char *TAG = "WEATHER_STATION";
//static const char *RAIN_TAG = "RAIN_GAUGE";

/* Network connection status - declared early for LED functions */
static bool zigbee_network_connected = false;

#ifdef DEBUG_LED_ENABLE
/* LED debug control - can be toggled via Zigbee endpoint 4 */
static bool led_debug_enabled = true;  // Default ON, can be controlled via genOnOff cluster

#if DEBUG_LED_TYPE_RGB
#include "led_strip.h"

/* Debug LED variables for RGB LED */
static led_strip_handle_t led_strip = NULL;
static bool led_blink_task_running = false;
static TaskHandle_t led_blink_task_handle = NULL;

/* LED control functions for WS2812 RGB LED */
static void debug_led_init(void)
{
    /* LED strip configuration for WS2812 (older API for led_strip ~2.0.0) */
    led_strip_config_t strip_config = {
        .strip_gpio_num = DEBUG_LED_GPIO,
        .max_leds = 1,                         // Only 1 LED
    };
    
    /* LED strip RMT configuration (older API) */
    led_strip_rmt_config_t rmt_config = {
        .resolution_hz = 10 * 1000 * 1000,     // 10MHz
    };
    
    ESP_ERROR_CHECK(led_strip_new_rmt_device(&strip_config, &rmt_config, &led_strip));
    
    /* Turn off LED initially */
    led_strip_clear(led_strip);
    ESP_LOGI(TAG, "Debug RGB LED initialized on GPIO%d (WS2812)", DEBUG_LED_GPIO);
}

static void debug_led_set(bool state)
{
    if (!led_debug_enabled) {
        led_strip_clear(led_strip);
        return;
    }
    if (state) {
        /* Blue color for connected state */
        led_strip_set_pixel(led_strip, 0, 0, 0, 16);  // R=0, G=0, B=16 (dim blue)
    } else {
        /* Off */
        led_strip_set_pixel(led_strip, 0, 0, 0, 0);
    }
    led_strip_refresh(led_strip);
}

static void debug_led_set_orange(void)
{
    if (!led_debug_enabled) {
        led_strip_clear(led_strip);
        return;
    }
    /* Orange color for network leave */
    led_strip_set_pixel(led_strip, 0, 16, 8, 0);  // R=16, G=8, B=0 (dim orange)
    led_strip_refresh(led_strip);
}

static void debug_led_blink_task(void *arg)
{
    while (led_blink_task_running) {
        /* Yellow blink for network joining */
        led_strip_set_pixel(led_strip, 0, 16, 8, 0);  // R=16, G=8, B=0 (dim yellow/orange)
        led_strip_refresh(led_strip);
        vTaskDelay(pdMS_TO_TICKS(500));  // ON for 500ms
        
        led_strip_set_pixel(led_strip, 0, 0, 0, 0);   // OFF
        led_strip_refresh(led_strip);
        vTaskDelay(pdMS_TO_TICKS(500));  // OFF for 500ms
    }
    vTaskDelete(NULL);
}

static void debug_led_start_blink(void)
{
    if (!led_debug_enabled) return;
    
    if (!led_blink_task_running) {
        led_blink_task_running = true;
        xTaskCreate(debug_led_blink_task, "led_blink", 2048, NULL, 5, &led_blink_task_handle);
        ESP_LOGI(TAG, "RGB LED blink started (network joining)");
    }
}

static void debug_led_stop_blink(void)
{
    if (led_blink_task_running) {
        led_blink_task_running = false;
        if (led_blink_task_handle) {
            vTaskDelay(pdMS_TO_TICKS(10));  // Give task time to exit
            led_blink_task_handle = NULL;
        }
    }
}

static void debug_led_rain_flash(void)
{
    if (!led_debug_enabled) return;
    
    /* Quick white flash for rain pulse (non-blocking) */
    led_strip_set_pixel(led_strip, 0, 16, 16, 16);  // R=16, G=16, B=16 (dim white)
    led_strip_refresh(led_strip);
    vTaskDelay(pdMS_TO_TICKS(100));  // Flash for 100ms
    /* Return to previous state: always solid blue if connected */
    if (zigbee_network_connected) {
        led_strip_set_pixel(led_strip, 0, 0, 0, 16);  // Back to blue (connected)
    } else {
        led_strip_set_pixel(led_strip, 0, 0, 0, 0);   // Back to off
    }
    led_strip_refresh(led_strip);
}

#else
/* Debug LED variables for simple GPIO LED */
static bool led_blink_task_running = false;
static TaskHandle_t led_blink_task_handle = NULL;

/* LED control functions */
static void debug_led_init(void)
{
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << DEBUG_LED_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);
    gpio_set_level(DEBUG_LED_GPIO, 0);  // LED OFF initially
    ESP_LOGI(TAG, "Debug LED initialized on GPIO%d", DEBUG_LED_GPIO);
}

static void debug_led_set(bool state)
{
    if (!led_debug_enabled) {
        gpio_set_level(DEBUG_LED_GPIO, 0);
        return;
    }
    gpio_set_level(DEBUG_LED_GPIO, state ? 1 : 0);
}

static void debug_led_blink_task(void *arg)
{
    while (led_blink_task_running) {
        gpio_set_level(DEBUG_LED_GPIO, 1);
        vTaskDelay(pdMS_TO_TICKS(500));  // ON for 500ms
        gpio_set_level(DEBUG_LED_GPIO, 0);
        vTaskDelay(pdMS_TO_TICKS(500));  // OFF for 500ms
    }
    vTaskDelete(NULL);
}

static void debug_led_start_blink(void)
{
    if (!led_debug_enabled) return;
    
    if (!led_blink_task_running) {
        led_blink_task_running = true;
        xTaskCreate(debug_led_blink_task, "led_blink", 2048, NULL, 5, &led_blink_task_handle);
        ESP_LOGI(TAG, "LED blink started (network joining)");
    }
}

static void debug_led_stop_blink(void)
{
    if (led_blink_task_running) {
        led_blink_task_running = false;
        if (led_blink_task_handle) {
            vTaskDelay(pdMS_TO_TICKS(10));  // Give task time to exit
            led_blink_task_handle = NULL;
        }
    }
}

static void debug_led_rain_flash(void)
{
    if (!led_debug_enabled) return;
    
    /* Quick flash for rain pulse (simple GPIO) */
    gpio_set_level(DEBUG_LED_GPIO, 1);
    vTaskDelay(pdMS_TO_TICKS(100));  // Flash for 100ms
    
    /* Return to previous state */
    gpio_set_level(DEBUG_LED_GPIO, zigbee_network_connected ? 1 : 0);
}
#endif /* DEBUG_LED_TYPE_RGB */
#else
/* Dummy functions when LED debug is disabled */
static inline void debug_led_init(void) {}
static inline void debug_led_set(bool state) {}
static inline void debug_led_start_blink(void) {}
static inline void debug_led_stop_blink(void) {}
static inline void debug_led_rain_flash(void) {}
#endif /* DEBUG_LED_ENABLE */

/* Rain gauge configuration */
#ifdef CONFIG_IDF_TARGET_ESP32H2
#define RAIN_GAUGE_GPIO         12              // GPIO pin for rain gauge reed switch (RTC-capable on ESP32-H2)
#else
#define RAIN_GAUGE_GPIO         5               // GPIO pin for rain gauge reed switch (RTC-capable on ESP32-C6)
#endif
#define RAIN_MM_PER_PULSE       0.36f           // mm of rain per bucket tip (adjust for your sensor)

/* Rain gauge variables */
static QueueHandle_t rain_gauge_evt_queue = NULL;
static float total_rainfall_mm = 0.0f;
static uint32_t rain_pulse_count = 0;
static const char *RAIN_TAG = "RAIN_GAUGE";
static bool rain_gauge_enabled = false;  // Only enable when connected to network
static bool rain_gauge_isr_installed = false;  // Track ISR installation state
static float last_reported_rainfall_mm = 0.0f;  // Track last reported value for 1mm threshold
static TickType_t last_report_time = 0;  // Track last report time for hourly reporting

/* Sleep configuration variables */
static uint32_t sleep_duration_seconds = SLEEP_DURATION_S;  // Default 15 minutes (900 seconds)
static const char *SLEEP_CONFIG_TAG = "SLEEP_CONFIG";
#define SLEEP_CONFIG_NVS_NAMESPACE      "sleep_config"
#define SLEEP_CONFIG_NVS_KEY            "duration_sec"
#define SLEEP_CONFIG_ATTR_ID            0x8000  // Custom attribute ID for sleep duration

/* Network connection status (zigbee_network_connected declared earlier for LED functions) */
static uint32_t connection_retry_count = 0;
static bool deep_sleep_scheduled = false;  // Track if deep sleep has been scheduled
#define NETWORK_RETRY_SLEEP_DURATION    30      // 30 seconds for network retry
#define MAX_CONNECTION_RETRIES          20      // Max retries before giving up (10 minutes total)

/* Button action tracking (no state needed for action-based buttons) */

/********************* Define functions **************************/
static void builtin_button_callback(button_action_t action);
static void factory_reset_device(uint8_t param);
static void bme280_read_and_report(uint8_t param);
static void prepare_for_deep_sleep(uint8_t param);
static void check_ota_status(uint8_t param);
static esp_err_t sleep_config_load(void);
static esp_err_t sleep_config_save(void);
static void rain_gauge_init(void);
static void rain_gauge_isr_handler(void *arg);
static void rain_gauge_task(void *arg);
static void rain_gauge_zigbee_update(uint8_t param);
static void rain_gauge_enable_isr(void);
static void rain_gauge_disable_isr(void);
static bool rain_gauge_should_report(void);
static esp_err_t battery_adc_init(void);
static void battery_read_and_report(uint8_t param);
static void sleep_duration_report(uint8_t param);
static void rain_gauge_hourly_check(uint8_t param);
static esp_err_t deferred_driver_init(void)
{
    /* Initialize builtin button with callback for factory reset */
    if (!builtin_button_driver_init(builtin_button_callback)) {
        ESP_LOGE(TAG, "Failed to initialize builtin button driver");
        return ESP_FAIL;
    }
    
    /* Initialize I2C and BME280 sensor */
    i2c_config_t i2c_cfg = {
        .mode = I2C_MODE_MASTER,
#ifdef CONFIG_IDF_TARGET_ESP32H2
        .sda_io_num = GPIO_NUM_10,     // ESP32-H2 I2C SDA
        .scl_io_num = GPIO_NUM_11,     // ESP32-H2 I2C SCL
#else
        .sda_io_num = GPIO_NUM_6,      // ESP32-C6 default I2C SDA
        .scl_io_num = GPIO_NUM_7,      // ESP32-C6 default I2C SCL
#endif
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = 100000,    // 100 KHz
    };
    
    i2c_bus_handle_t i2c_bus = i2c_bus_create(I2C_NUM_0, &i2c_cfg);
    if (i2c_bus == NULL) {
        ESP_LOGE(TAG, "Failed to create I2C bus");
        return ESP_FAIL;
    }
    
    esp_err_t ret = bme280_app_init(i2c_bus);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize BME280 sensor: %s", esp_err_to_name(ret));
        /* Don't fail completely if BME280 is not connected */
        ESP_LOGW(TAG, "Continuing without BME280 sensor");
    } else {
#ifdef CONFIG_IDF_TARGET_ESP32H2
        ESP_LOGI(TAG, "BME280 sensor initialized successfully on ESP32-H2 (SDA:GPIO10, SCL:GPIO11)");
#else
        ESP_LOGI(TAG, "BME280 sensor initialized successfully on ESP32-C6 (SDA:GPIO6, SCL:GPIO7)");
#endif
    }
    
    /* Initialize rain gauge */
    rain_gauge_init();
    
    /* Initialize battery ADC */
    ret = battery_adc_init();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Battery ADC initialization failed, will use simulated values");
    }
    
    /* Load sleep configuration */
    sleep_config_load();
    
    return ESP_OK;
}

static void bdb_start_top_level_commissioning_cb(uint8_t mode_mask)
{
    ESP_RETURN_ON_FALSE(esp_zb_bdb_start_top_level_commissioning(mode_mask) == ESP_OK, , TAG, "Failed to start Zigbee commissioning");
}

void esp_zb_app_signal_handler(esp_zb_app_signal_t *signal_struct)
{
    uint32_t *p_sg_p       = signal_struct->p_app_signal;
    esp_err_t err_status = signal_struct->esp_err_status;
    esp_zb_app_signal_type_t sig_type = *p_sg_p;
    switch (sig_type) {
    case ESP_ZB_ZDO_SIGNAL_SKIP_STARTUP:
        ESP_LOGI(TAG, "Initialize Zigbee stack");
        debug_led_start_blink();  // Start blinking when joining network
        esp_zb_bdb_start_top_level_commissioning(ESP_ZB_BDB_MODE_INITIALIZATION);
        break;
    case ESP_ZB_ZDO_SIGNAL_LEAVE:
        ESP_LOGI(TAG, "Device is leaving the Zigbee network");
        debug_led_stop_blink();
        debug_led_set_orange();
        zigbee_network_connected = false;
        rain_gauge_disable_isr();
        break;
    case ESP_ZB_BDB_SIGNAL_DEVICE_FIRST_START:
    case ESP_ZB_BDB_SIGNAL_DEVICE_REBOOT:
        /* Always initialize drivers regardless of Zigbee stack status */
        ESP_LOGI(TAG, "Deferred driver initialization %s", deferred_driver_init() ? "failed" : "successful");
        
        if (err_status == ESP_OK) {
            ESP_LOGI(TAG, "Device started up in %s factory-reset mode", esp_zb_bdb_is_factory_new() ? "" : "non");
            if (esp_zb_bdb_is_factory_new()) {
                ESP_LOGI(TAG, "Start network steering");
                debug_led_start_blink();  // Start blinking when joining network
                esp_zb_bdb_start_top_level_commissioning(ESP_ZB_BDB_MODE_NETWORK_STEERING);
            } else {
                ESP_LOGI(TAG, "Device rebooted - was previously connected");
                debug_led_stop_blink();  // Stop blinking
                debug_led_set(true);     // LED ON - connected
                /* Device was previously paired - mark as connected and enable rain gauge */
                zigbee_network_connected = true;
                rain_gauge_enable_isr();
                ESP_LOGI(TAG, "📡 Re-enabled rain gauge after reboot (previously connected network)");
                
                /* Schedule sensor data reporting and sleep after wake-up from deep sleep */
                ESP_LOGI(TAG, "📊 Scheduling sensor data reporting after wake-up");
                esp_zb_scheduler_alarm((esp_zb_callback_t)bme280_read_and_report, 0, 2000); // Report in 2 seconds
                esp_zb_scheduler_alarm((esp_zb_callback_t)rain_gauge_zigbee_update, 0, 3000); // Report rainfall in 3 seconds
                esp_zb_scheduler_alarm((esp_zb_callback_t)battery_read_and_report, 0, 4000); // Report battery in 4 seconds
                esp_zb_scheduler_alarm((esp_zb_callback_t)sleep_duration_report, 0, 5000); // Report sleep duration in 5 seconds
                
                /* Check if OTA is in progress before scheduling deep sleep */
                if (esp_zb_ota_is_active()) {
                    ESP_LOGW(TAG, "🔄 OTA update in progress - staying awake, deep sleep disabled");
                    deep_sleep_scheduled = false; // Don't schedule sleep during OTA
                    /* Schedule periodic OTA status check */
                    esp_zb_scheduler_alarm((esp_zb_callback_t)check_ota_status, 0, 5000); // Check every 5 seconds
                } else {
                    /* Schedule deep sleep after reporting window */
                    if (!deep_sleep_scheduled) {
                        ESP_LOGI(TAG, "⏰ Scheduling deep sleep after reporting window (15 seconds)");
                        esp_zb_scheduler_alarm((esp_zb_callback_t)prepare_for_deep_sleep, 0, 15000);
                        deep_sleep_scheduled = true;
                    }
                }
            }
        } else {
            /* commissioning failed - try to rejoin */
            ESP_LOGW(TAG, "Failed to initialize Zigbee stack (status: %s), attempting to rejoin", esp_err_to_name(err_status));
            if (!esp_zb_bdb_is_factory_new()) {
                /* Device was previously connected, try to rejoin */
                ESP_LOGI(TAG, "Attempting to rejoin previous network");
                esp_zb_bdb_start_top_level_commissioning(ESP_ZB_BDB_MODE_NETWORK_STEERING);
            }
        }
        break;
    case ESP_ZB_BDB_SIGNAL_STEERING:
        if (err_status == ESP_OK) {
            esp_zb_ieee_addr_t extended_pan_id;
            esp_zb_get_extended_pan_id(extended_pan_id);
            ESP_LOGI(TAG, "Joined network successfully (Extended PAN ID: %02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x, PAN ID: 0x%04hx, Channel:%d, Short Address: 0x%04hx)",
                     extended_pan_id[7], extended_pan_id[6], extended_pan_id[5], extended_pan_id[4],
                     extended_pan_id[3], extended_pan_id[2], extended_pan_id[1], extended_pan_id[0],
                     esp_zb_get_pan_id(), esp_zb_get_current_channel(), esp_zb_get_short_address());
            
            debug_led_stop_blink();  // Stop blinking
            debug_led_set(true);     // LED ON - connected
            
            /* Mark network as connected and reset retry count */
            zigbee_network_connected = true;
            connection_retry_count = 0;
            
            /* Enable rain gauge now that we're connected */
            rain_gauge_enable_isr();
            ESP_LOGI(RAIN_TAG, "Rain gauge enabled - device connected to Zigbee network");
            
            /* Schedule sensor data reporting after first connection */
            ESP_LOGI(TAG, "📊 Scheduling initial sensor data reporting after network join");
            esp_zb_scheduler_alarm((esp_zb_callback_t)bme280_read_and_report, 0, 2000); // Report in 2 seconds
            esp_zb_scheduler_alarm((esp_zb_callback_t)rain_gauge_zigbee_update, 0, 3000); // Report rainfall in 3 seconds
            esp_zb_scheduler_alarm((esp_zb_callback_t)battery_read_and_report, 0, 4000); // Report battery in 4 seconds
            esp_zb_scheduler_alarm((esp_zb_callback_t)sleep_duration_report, 0, 5000); // Report sleep duration in 5 seconds
            
            /* Check if OTA is in progress before scheduling deep sleep */
            if (esp_zb_ota_is_active()) {
                ESP_LOGW(TAG, "🔄 OTA update in progress - staying awake, deep sleep disabled");
                deep_sleep_scheduled = false; // Don't schedule sleep during OTA
                /* Schedule periodic OTA status check */
                esp_zb_scheduler_alarm((esp_zb_callback_t)check_ota_status, 0, 5000); // Check every 5 seconds
            } else {
                /* Now that we're connected, schedule normal deep sleep after allowing time for data reporting */
                if (!deep_sleep_scheduled) {
                    ESP_LOGI(TAG, "📡 Network connected! Scheduling sleep after sensor data reporting (15 seconds)");
                    esp_zb_scheduler_alarm((esp_zb_callback_t)prepare_for_deep_sleep, 0, 15000); // 15 seconds for reporting
                    deep_sleep_scheduled = true;
                }
            }
        } else {
            ESP_LOGI(TAG, "Network steering was not successful (status: %s)", esp_err_to_name(err_status));
            
            /* Mark network as disconnected and increment retry count */
            zigbee_network_connected = false;
            connection_retry_count++;
            
            /* Disable rain gauge when not connected */
            rain_gauge_disable_isr();
            ESP_LOGW(RAIN_TAG, "Rain gauge disabled - not connected to network");
            
            ESP_LOGW(TAG, "🔄 Connection attempt %d/%d failed", connection_retry_count, MAX_CONNECTION_RETRIES);
            esp_zb_scheduler_alarm((esp_zb_callback_t)bdb_start_top_level_commissioning_cb, ESP_ZB_BDB_MODE_NETWORK_STEERING, 1000);
        }
        break;
    default:
        ESP_LOGI(TAG, "ZDO signal: %s (0x%x), status: %s", esp_zb_zdo_signal_to_string(sig_type), sig_type,
                 esp_err_to_name(err_status));
        break;
    }
}

static esp_err_t zb_attribute_handler(const esp_zb_zcl_set_attr_value_message_t *message)
{
    esp_err_t ret = ESP_OK;

    ESP_RETURN_ON_FALSE(message, ESP_FAIL, TAG, "Empty message");
    ESP_RETURN_ON_FALSE(message->info.status == ESP_ZB_ZCL_STATUS_SUCCESS, ESP_ERR_INVALID_ARG, TAG, "Received message: error status(%d)",
                        message->info.status);
    ESP_LOGI(TAG, "Received message: endpoint(%d), cluster(0x%x), attribute(0x%x), data size(%d)", message->info.dst_endpoint, message->info.cluster,
             message->attribute.id, message->attribute.data.size);
    
    /* Handle sleep duration configuration via analog input cluster */
    if (message->info.dst_endpoint == HA_ESP_SLEEP_CONFIG_ENDPOINT && 
        message->info.cluster == ESP_ZB_ZCL_CLUSTER_ID_ANALOG_INPUT &&
        message->attribute.id == ESP_ZB_ZCL_ATTR_ANALOG_INPUT_PRESENT_VALUE_ID) {
        
        if (message->attribute.data.size == sizeof(float)) {
            float new_duration_float = *(float*)message->attribute.data.value;
            uint32_t new_duration = (uint32_t)new_duration_float;
            
            // Validate range: 60 seconds (1 minute) to 7200 seconds (2 hours)
            if (new_duration >= 60 && new_duration <= 7200) {
                sleep_duration_seconds = new_duration;
                sleep_config_save();
                
                /* Update the analog input present value to reflect the accepted value */
                float accepted_value = (float)sleep_duration_seconds;
                esp_zb_zcl_set_attribute_val(HA_ESP_SLEEP_CONFIG_ENDPOINT, ESP_ZB_ZCL_CLUSTER_ID_ANALOG_INPUT,
                                           ESP_ZB_ZCL_CLUSTER_SERVER_ROLE, ESP_ZB_ZCL_ATTR_ANALOG_INPUT_PRESENT_VALUE_ID,
                                           &accepted_value, false);
                
                ESP_LOGI(SLEEP_CONFIG_TAG, "🔧 Sleep duration updated to %d seconds (%.1f minutes) via Zigbee", 
                         sleep_duration_seconds, sleep_duration_seconds / 60.0f);
            } else {
                ESP_LOGW(SLEEP_CONFIG_TAG, "Invalid sleep duration %d (must be 60-7200 seconds)", new_duration);
                ret = ESP_ERR_INVALID_ARG;
            }
        } else {
            ESP_LOGW(SLEEP_CONFIG_TAG, "Invalid data size for sleep duration: %d", message->attribute.data.size);
            ret = ESP_ERR_INVALID_SIZE;
        }
    }
    
    /* Handle LED debug control via On/Off cluster on primary endpoint (EP1) */
    if (message->info.dst_endpoint == HA_ESP_BME280_ENDPOINT && 
        message->info.cluster == ESP_ZB_ZCL_CLUSTER_ID_ON_OFF &&
        message->attribute.id == ESP_ZB_ZCL_ATTR_ON_OFF_ON_OFF_ID) {
        
        ESP_LOGI(TAG, "🔧 LED debug control message received on primary endpoint %d", HA_ESP_BME280_ENDPOINT);
        
        if (message->attribute.data.size == sizeof(bool)) {
            bool new_state = *(bool*)message->attribute.data.value;
            bool prev_state = led_debug_enabled;
            led_debug_enabled = new_state;
            
            ESP_LOGI(TAG, "💡 LED debug switch: %s → %s via Zigbee", 
                     prev_state ? "ON" : "OFF", 
                     new_state ? "ON" : "OFF");
            
            /* Update the LED immediately based on new state and network status */
            if (led_debug_enabled) {
                /* Re-enable LED, set to current network state */
                debug_led_set(zigbee_network_connected);
                ESP_LOGI(TAG, "✅ LED debug enabled - LED set to %s", 
                         zigbee_network_connected ? "BLUE (connected)" : "OFF (disconnected)");
            } else {
                /* Disable LED - turn it off */
                debug_led_stop_blink();
                debug_led_set(false);
                ESP_LOGI(TAG, "❌ LED debug disabled - LED turned OFF");
            }
        } else {
            ESP_LOGW(TAG, "Invalid data size for LED debug control: %d", message->attribute.data.size);
            ret = ESP_ERR_INVALID_SIZE;
        }
    }
    
    return ret;
}

static esp_err_t zb_action_handler(esp_zb_core_action_callback_id_t callback_id, const void *message)
{
    esp_err_t ret = ESP_OK;
    switch (callback_id) {
    case ESP_ZB_CORE_SET_ATTR_VALUE_CB_ID:
        ret = zb_attribute_handler((esp_zb_zcl_set_attr_value_message_t *)message);
        break;
    case ESP_ZB_CORE_OTA_UPGRADE_VALUE_CB_ID:
        ret = zb_ota_upgrade_value_handler(*(esp_zb_zcl_ota_upgrade_value_message_t *)message);
        break;
    case ESP_ZB_CORE_OTA_UPGRADE_QUERY_IMAGE_RESP_CB_ID:
        ret = zb_ota_query_image_resp_handler(*(esp_zb_zcl_ota_upgrade_query_image_resp_message_t *)message);
        break;
    case ESP_ZB_CORE_REPORT_ATTR_CB_ID:
        {
            esp_zb_zcl_report_attr_message_t *report_attr_message = (esp_zb_zcl_report_attr_message_t *)message;
            
            // Simplified logging - only show key information
            if (report_attr_message->cluster == ESP_ZB_ZCL_CLUSTER_ID_ANALOG_INPUT) {
                float analog_value = report_attr_message->attribute.data.value ? 
                                    *(float*)report_attr_message->attribute.data.value : 0.0f;
                
                if (report_attr_message->attribute.id == ESP_ZB_ZCL_ATTR_ANALOG_INPUT_PRESENT_VALUE_ID) {
                    /* Check which endpoint this is from */
                    if (report_attr_message->dst_endpoint == HA_ESP_RAIN_GAUGE_ENDPOINT) {
                        /* This is rain gauge total rainfall */
                        ESP_LOGI(TAG, "📡 Rain: %.2f mm", analog_value);
                    }
                }
            } else if (report_attr_message->cluster == ESP_ZB_ZCL_CLUSTER_ID_TEMP_MEASUREMENT) {
                int16_t temp_raw = report_attr_message->attribute.data.value ? 
                                 *(int16_t*)report_attr_message->attribute.data.value : 0;
                ESP_LOGI(TAG, "📡 Temp: %.1f°C", temp_raw / 100.0f);
            } else if (report_attr_message->cluster == ESP_ZB_ZCL_CLUSTER_ID_REL_HUMIDITY_MEASUREMENT) {
                uint16_t hum_raw = report_attr_message->attribute.data.value ? 
                                 *(uint16_t*)report_attr_message->attribute.data.value : 0;
                ESP_LOGI(TAG, "📡 Humidity: %.1f%%", hum_raw / 100.0f);
            } else if (report_attr_message->cluster == ESP_ZB_ZCL_CLUSTER_ID_PRESSURE_MEASUREMENT) {
                int16_t pressure_raw = report_attr_message->attribute.data.value ? 
                                     *(int16_t*)report_attr_message->attribute.data.value : 0;
                ESP_LOGI(TAG, "📡 Pressure: %.1f hPa", pressure_raw / 10.0f);
            } else {
                // Minimal logging for other clusters
                ESP_LOGD(TAG, "📤 Report: EP%d, cluster 0x%x", 
                         report_attr_message->dst_endpoint, report_attr_message->cluster);
            }
        }
        break;
    case 0x1005:  // Command response callback - normal Zigbee operation (attribute update acknowledgments)
        ESP_LOGD(TAG, "Zigbee command response received (0x1005) - normal operation");
        break;
    default:
        ESP_LOGW(TAG, "Receive Zigbee action(0x%x) callback", callback_id);
        ESP_LOGW(TAG, "  ESP_ZB_CORE_SET_ATTR_VALUE_CB_ID = 0x%x", ESP_ZB_CORE_SET_ATTR_VALUE_CB_ID);
        ESP_LOGW(TAG, "  ESP_ZB_CORE_REPORT_ATTR_CB_ID = 0x%x", ESP_ZB_CORE_REPORT_ATTR_CB_ID);
        break;
    }
    return ret;
}

/**
 * @brief Prepare for deep sleep - called by Zigbee scheduler after operations complete
 */
static void prepare_for_deep_sleep(uint8_t param)
{
    /* Check if OTA upgrade is in progress */
    if (esp_zb_ota_is_active()) {
        ESP_LOGW(TAG, "⚠️ OTA upgrade in progress, postponing deep sleep...");
        /* Reschedule sleep check in 30 seconds */
        esp_zb_scheduler_alarm((esp_zb_callback_t)prepare_for_deep_sleep, 0, 30000);
        return;
    }
    
    ESP_LOGI(TAG, "✅ Zigbee operations complete, preparing for deep sleep...");
    
    /* Turn off LED before sleep */
    debug_led_stop_blink();
    debug_led_set(false);
    
    /* Force final sensor reporting before sleep */
    if (zigbee_network_connected) {
        ESP_LOGI(TAG, "📊 Sending final sensor reports before deep sleep...");
        bme280_read_and_report(0);
        vTaskDelay(pdMS_TO_TICKS(100)); // Brief delay between reports
        rain_gauge_zigbee_update(0);
        vTaskDelay(pdMS_TO_TICKS(100));
        battery_read_and_report(0);
        vTaskDelay(pdMS_TO_TICKS(500)); // Allow time for Zigbee transmission
    }
    
    /* Reset deep sleep scheduling flag */
    deep_sleep_scheduled = false;
    
    /* Save rainfall data before sleeping */
    float current_rainfall = total_rainfall_mm;
    uint32_t current_pulses = rain_pulse_count;
    save_rainfall_data(current_rainfall, current_pulses);
    
    /* Determine sleep duration based on network connection status */
    uint32_t sleep_duration;
    
    if (!zigbee_network_connected) {
        if (connection_retry_count >= MAX_CONNECTION_RETRIES) {
            /* After max retries, use longer sleep to save battery but keep trying */
            sleep_duration = sleep_duration_seconds / 2;  // Use half the configured duration
            ESP_LOGW(TAG, "🔋 Max connection retries reached, using reduced sleep duration: %lu seconds", sleep_duration);
        } else {
            /* Not connected - short sleep for quick retry */
            sleep_duration = NETWORK_RETRY_SLEEP_DURATION;
            ESP_LOGW(TAG, "📡 Not connected to network, using short sleep for retry: %lu seconds (attempt %d/%d)", 
                     sleep_duration, connection_retry_count + 1, MAX_CONNECTION_RETRIES);
        }
    } else {
        /* Connected - use normal adaptive sleep duration */
        sleep_duration = get_adaptive_sleep_duration(0.0f, sleep_duration_seconds);
        ESP_LOGI(TAG, "� Connected to network, using normal sleep duration: %lu seconds (%.1f minutes)", 
                 sleep_duration, sleep_duration / 60.0f);
    }
    
    ESP_LOGI(TAG, "💤 Entering deep sleep for %lu seconds...", sleep_duration);
    
    /* Give time for log output */
    vTaskDelay(pdMS_TO_TICKS(100));

    /* Update rain gauge value before sleep */
    rain_gauge_zigbee_update(0); // Send last rain value
    vTaskDelay(pdMS_TO_TICKS(100)); // Allow time for Zigbee transmission
    /* Suspend Zigbee stack before deep sleep (required for ESP32-H2) */
    ESP_LOGI(TAG, "📴 Suspending Zigbee stack before deep sleep...");
    esp_zb_sleep_enable(true);
    vTaskDelay(pdMS_TO_TICKS(100));
    
    /* Enter deep sleep with timer and GPIO wake-up enabled */
    enter_deep_sleep(sleep_duration, true);  // Enable rain detection wake-up
    
    /* Never reached - device will restart after wake-up */
}

/**
 * @brief Check OTA status and reschedule deep sleep when OTA completes
 */
static void check_ota_status(uint8_t param)
{
    if (esp_zb_ota_is_active()) {
        /* OTA still in progress - stay awake and check again in 5 seconds */
        ESP_LOGI(TAG, "🔄 OTA update in progress, staying awake...");
        esp_zb_scheduler_alarm((esp_zb_callback_t)check_ota_status, 0, 5000);
    } else {
        /* OTA completed or not active - schedule deep sleep */
        ESP_LOGI(TAG, "✅ OTA check complete, scheduling deep sleep");
        if (!deep_sleep_scheduled && zigbee_network_connected) {
            esp_zb_scheduler_alarm((esp_zb_callback_t)prepare_for_deep_sleep, 0, 5000);
            deep_sleep_scheduled = true;
        }
    }
}

// Helper to fill a Zigbee ZCL string (first byte = length, then chars)
static void fill_zcl_string(char *buf, size_t bufsize, const char *src) {
    size_t len = strlen(src);
    if (len > bufsize - 1) len = bufsize - 1; // Reserve 1 byte for length
    buf[0] = (uint8_t)len;
    memcpy(&buf[1], src, len);
}

static void esp_zb_task(void *pvParameters)
{
    /* initialize Zigbee stack */
    esp_zb_cfg_t zb_nwk_cfg = ESP_ZB_ZED_CONFIG();
    esp_zb_init(&zb_nwk_cfg);
    
    /* Create endpoint list */
    esp_zb_ep_list_t *esp_zb_ep_list = esp_zb_ep_list_create();


    /* Create BME280 environmental sensor endpoint */
    esp_zb_cluster_list_t *esp_zb_bme280_clusters = esp_zb_zcl_cluster_list_create();
    
    /* Create Basic cluster for BME280 endpoint */
    esp_zb_basic_cluster_cfg_t basic_bme280_cfg = {
        .zcl_version = ESP_ZB_ZCL_BASIC_ZCL_VERSION_DEFAULT_VALUE,
        .power_source = 0x03,  // 0x03 = Battery
    };
    esp_zb_attribute_list_t *esp_zb_basic_bme280_cluster = esp_zb_basic_cluster_create(&basic_bme280_cfg);
    
    /* Add optional Basic cluster attributes for Zigbee2MQTT */
#ifdef FW_VERSION_MAJOR
    int ver_major = FW_VERSION_MAJOR;
    int ver_minor = FW_VERSION_MINOR;
#else
    int ver_major = 1, ver_minor = 1;
#endif
    
    // Encode version as (major << 4) | minor for Zigbee compatibility
    uint8_t app_version = (ver_major << 4) | ver_minor;  // e.g., 1.1 = 0x11
    
#ifdef ZB_STACK_VERSION
    uint8_t stack_version = (ZB_STACK_VERSION << 4) | 0;
#else
    uint8_t stack_version = 0x30; // Zigbee 3.0
#endif
    
    // Use CMake-injected version and date for Zigbee attributes
    char date_code[17];      // 16 chars max for Zigbee date code
    char sw_build_id[17];    // 16 chars max for Zigbee sw_build_id
    uint8_t hw_version = 1;
    
/* Use generated version.h values (configure_file -> build/generated/version.h) */
    ESP_LOGI(TAG, "Using generated version: FW_VERSION=%s, FW_DATE_CODE=%s", FW_VERSION, FW_DATE_CODE);
    fill_zcl_string(date_code, sizeof(date_code), FW_DATE_CODE);
    fill_zcl_string(sw_build_id, sizeof(sw_build_id), FW_VERSION);
    
    esp_zb_basic_cluster_add_attr(esp_zb_basic_bme280_cluster, ESP_ZB_ZCL_ATTR_BASIC_APPLICATION_VERSION_ID, &app_version);
    esp_zb_basic_cluster_add_attr(esp_zb_basic_bme280_cluster, ESP_ZB_ZCL_ATTR_BASIC_STACK_VERSION_ID, &stack_version);
    esp_zb_basic_cluster_add_attr(esp_zb_basic_bme280_cluster, ESP_ZB_ZCL_ATTR_BASIC_HW_VERSION_ID, &hw_version);
    esp_zb_basic_cluster_add_attr(esp_zb_basic_bme280_cluster, ESP_ZB_ZCL_ATTR_BASIC_DATE_CODE_ID, (void*)date_code);
    esp_zb_basic_cluster_add_attr(esp_zb_basic_bme280_cluster, ESP_ZB_ZCL_ATTR_BASIC_SW_BUILD_ID, (void*)sw_build_id);
    
    ESP_ERROR_CHECK(esp_zb_cluster_list_add_basic_cluster(esp_zb_bme280_clusters, esp_zb_basic_bme280_cluster, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE));
    
    /* Create Temperature measurement cluster */
    esp_zb_temperature_meas_cluster_cfg_t temp_meas_cfg = {
        .measured_value = 0x8000,  // Invalid/unknown temperature value
        .min_value = -40 * 100,     // -40°C in centidegrees
        .max_value = 85 * 100,      // 85°C in centidegrees (BME280 range)
    };
    esp_zb_attribute_list_t *esp_zb_temperature_cluster = esp_zb_temperature_meas_cluster_create(&temp_meas_cfg);
    ESP_ERROR_CHECK(esp_zb_cluster_list_add_temperature_meas_cluster(esp_zb_bme280_clusters, esp_zb_temperature_cluster, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE));
    
    /* Create Humidity measurement cluster */
    esp_zb_humidity_meas_cluster_cfg_t hum_meas_cfg = {
        .measured_value = 0xFFFF,   // Invalid/unknown humidity value
        .min_value = 0 * 100,       // 0% in centipercent
        .max_value = 100 * 100,     // 100% in centipercent
    };
    esp_zb_attribute_list_t *esp_zb_humidity_cluster = esp_zb_humidity_meas_cluster_create(&hum_meas_cfg);
    ESP_ERROR_CHECK(esp_zb_cluster_list_add_humidity_meas_cluster(esp_zb_bme280_clusters, esp_zb_humidity_cluster, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE));
    
    /* Create Pressure measurement cluster */
    esp_zb_pressure_meas_cluster_cfg_t pressure_meas_cfg = {
        .measured_value = ESP_ZB_ZCL_ATTR_PRESSURE_MEASUREMENT_VALUE_UNKNOWN,  // Invalid/unknown pressure value initially
    };
    esp_zb_attribute_list_t *esp_zb_pressure_cluster = esp_zb_pressure_meas_cluster_create(&pressure_meas_cfg);
    ESP_ERROR_CHECK(esp_zb_cluster_list_add_pressure_meas_cluster(esp_zb_bme280_clusters, esp_zb_pressure_cluster, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE));
    
    /* Add Identify cluster for BME280 endpoint */
    ESP_ERROR_CHECK(esp_zb_cluster_list_add_identify_cluster(esp_zb_bme280_clusters, esp_zb_identify_cluster_create(NULL), ESP_ZB_ZCL_CLUSTER_SERVER_ROLE));
    
    /* Add Power Configuration cluster for battery monitoring */
    esp_zb_power_config_cluster_cfg_t power_cfg = {
        .main_voltage = 0xFFFF,           // Unknown initially (will be updated)
    };
    esp_zb_attribute_list_t *esp_zb_power_cluster = esp_zb_power_config_cluster_create(&power_cfg);
    
    /* Add battery-specific attributes using raw attribute IDs */
    uint8_t battery_voltage = 0xFF;       // Unknown initially (0.1V units, e.g., 37 = 3.7V)
    uint8_t battery_percentage = 0xFF;    // Unknown initially (0-200, where 200 = 100%)
    uint8_t battery_size = 0xFF;          // 0xFF = other/unknown
    uint8_t battery_quantity = 1;
    uint8_t battery_rated_voltage = 37;   // 3.7V nominal for Li-Ion
    uint8_t battery_alarm_mask = 0;
    uint8_t battery_voltage_min_threshold = 27;  // 2.7V low battery warning for Li-Ion
    
    esp_zb_power_config_cluster_add_attr(esp_zb_power_cluster, 0x0020, &battery_voltage);                    // Battery Voltage
    esp_zb_power_config_cluster_add_attr(esp_zb_power_cluster, 0x0021, &battery_percentage);                 // Battery Percentage Remaining
    esp_zb_power_config_cluster_add_attr(esp_zb_power_cluster, 0x0031, &battery_size);                       // Battery Size
    esp_zb_power_config_cluster_add_attr(esp_zb_power_cluster, 0x0033, &battery_quantity);                   // Battery Quantity
    esp_zb_power_config_cluster_add_attr(esp_zb_power_cluster, 0x0034, &battery_rated_voltage);              // Battery Rated Voltage
    esp_zb_power_config_cluster_add_attr(esp_zb_power_cluster, 0x0035, &battery_alarm_mask);                 // Battery Alarm Mask
    esp_zb_power_config_cluster_add_attr(esp_zb_power_cluster, 0x0036, &battery_voltage_min_threshold);      // Battery Voltage Min Threshold
    
    ESP_ERROR_CHECK(esp_zb_cluster_list_add_power_config_cluster(esp_zb_bme280_clusters, esp_zb_power_cluster, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE));

    /* Add OTA client cluster to BME280 endpoint for firmware updates */
#ifdef OTA_FILE_VERSION
    uint32_t ota_file_version = OTA_FILE_VERSION;
#else
    uint32_t ota_file_version = esp_zb_ota_get_fw_version();
#endif
    
    esp_zb_ota_cluster_cfg_t ota_cluster_cfg = {
        .ota_upgrade_file_version = ota_file_version,
        .ota_upgrade_manufacturer = OTA_UPGRADE_MANUFACTURER,
        .ota_upgrade_image_type = OTA_UPGRADE_IMAGE_TYPE,
        .ota_upgrade_downloaded_file_ver = 0xFFFFFFFF,    // No pending update
    };
    esp_zb_attribute_list_t *esp_zb_ota_client_cluster = esp_zb_ota_cluster_create(&ota_cluster_cfg);
    
    /* Add OTA cluster attributes for Zigbee2MQTT OTA version display */
    uint32_t current_file_version = ota_cluster_cfg.ota_upgrade_file_version;
    // Note: Attribute 0x0002 (currentZigbeeStackVersion) is managed by the Zigbee stack - don't add manually!
    esp_zb_ota_cluster_add_attr(esp_zb_ota_client_cluster, 0x0003, &current_file_version); // currentFileVersion
    
    /* Add client-specific OTA attributes */
    esp_zb_zcl_ota_upgrade_client_variable_t client_vars = {
        .timer_query = ESP_ZB_ZCL_OTA_UPGRADE_QUERY_TIMER_COUNT_DEF,
        .hw_version = 0x0101,
        .max_data_size = 223,
    };
    esp_zb_ota_cluster_add_attr(esp_zb_ota_client_cluster, ESP_ZB_ZCL_ATTR_OTA_UPGRADE_CLIENT_DATA_ID, &client_vars);
    
    /* Add server address and endpoint (broadcast initially) */
    uint16_t server_addr = 0xffff;
    esp_zb_ota_cluster_add_attr(esp_zb_ota_client_cluster, ESP_ZB_ZCL_ATTR_OTA_UPGRADE_SERVER_ADDR_ID, &server_addr);
    
    uint8_t server_ep = 0xff;
    esp_zb_ota_cluster_add_attr(esp_zb_ota_client_cluster, ESP_ZB_ZCL_ATTR_OTA_UPGRADE_SERVER_ENDPOINT_ID, &server_ep);
    
    ESP_ERROR_CHECK(esp_zb_cluster_list_add_ota_cluster(esp_zb_bme280_clusters, esp_zb_ota_client_cluster, ESP_ZB_ZCL_CLUSTER_CLIENT_ROLE));
    ESP_LOGI(TAG, "📦 OTA client cluster added to endpoint %d (version: 0x%08lX, mfr: 0x%04X, type: 0x%04X)", 
             HA_ESP_BME280_ENDPOINT, ota_file_version, OTA_UPGRADE_MANUFACTURER, OTA_UPGRADE_IMAGE_TYPE);

    /* Move LED debug On/Off control to primary endpoint (EP1).
     * Some controllers probe the primary endpoint for common clusters; placing
     * the genOnOff cluster on EP1 avoids UNSUPPORTED_CLUSTER responses when the
     * controller queries EP1. This does not interfere with temperature/humidity/
     * pressure clusters which remain on EP1 as separate clusters.
     */
    esp_zb_on_off_cluster_cfg_t onoff_cfg = {
        .on_off = led_debug_enabled,
    };
    esp_zb_attribute_list_t *esp_zb_onoff_cluster = esp_zb_on_off_cluster_create(&onoff_cfg);
    ESP_ERROR_CHECK(esp_zb_cluster_list_add_on_off_cluster(esp_zb_bme280_clusters, esp_zb_onoff_cluster, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE));

    esp_zb_endpoint_config_t endpoint_bme280_config = {
        .endpoint = HA_ESP_BME280_ENDPOINT,
        .app_profile_id = ESP_ZB_AF_HA_PROFILE_ID,
        .app_device_id = ESP_ZB_HA_TEMPERATURE_SENSOR_DEVICE_ID,
        .app_device_version = 0
    };
    esp_zb_ep_list_add_ep(esp_zb_ep_list, esp_zb_bme280_clusters, endpoint_bme280_config);    /* Create rain gauge sensor endpoint */
    esp_zb_cluster_list_t *esp_zb_rain_clusters = esp_zb_zcl_cluster_list_create();
    
    /* Basic cluster intentionally omitted for rain gauge endpoint (EP2).
     * We expose only the Analog Input cluster on this endpoint so the coordinator
     * does not attempt to read device-level Basic attributes here.
     */
    
    /* Create Analog Input cluster for rain gauge (reports rainfall in mm) */
    esp_zb_analog_input_cluster_cfg_t rain_analog_cfg = {
        .present_value = 0.0f,  // Will be set to total rainfall in mm
    };
    esp_zb_attribute_list_t *esp_zb_rain_analog_cluster = esp_zb_analog_input_cluster_create(&rain_analog_cfg);
    
    /* Add description attribute */
    char rain_description[] = "\x0E""Rainfall Total";  // Length-prefixed: 14 bytes + "Rainfall Total"
    esp_zb_analog_input_cluster_add_attr(esp_zb_rain_analog_cluster, ESP_ZB_ZCL_ATTR_ANALOG_INPUT_DESCRIPTION_ID, rain_description);
    
    /* Add engineering units attribute (mm) */
    uint16_t engineering_units = 0;  // 0 = dimensionless, could use custom units
    esp_zb_analog_input_cluster_add_attr(esp_zb_rain_analog_cluster, ESP_ZB_ZCL_ATTR_ANALOG_INPUT_ENGINEERING_UNITS_ID, &engineering_units);
    
    ESP_ERROR_CHECK(esp_zb_cluster_list_add_analog_input_cluster(esp_zb_rain_clusters, esp_zb_rain_analog_cluster, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE));
    
    /* Add Identify cluster for rain gauge endpoint */
    ESP_ERROR_CHECK(esp_zb_cluster_list_add_identify_cluster(esp_zb_rain_clusters, esp_zb_identify_cluster_create(NULL), ESP_ZB_ZCL_CLUSTER_SERVER_ROLE));
    
    esp_zb_endpoint_config_t endpoint_rain_config = {
        .endpoint = HA_ESP_RAIN_GAUGE_ENDPOINT,
        .app_profile_id = ESP_ZB_AF_HA_PROFILE_ID,
        .app_device_id = ESP_ZB_HA_SIMPLE_SENSOR_DEVICE_ID,
        .app_device_version = 0
    };
    esp_zb_ep_list_add_ep(esp_zb_ep_list, esp_zb_rain_clusters, endpoint_rain_config);

    /* CREATE SLEEP CONFIGURATION ENDPOINT */
    esp_zb_cluster_list_t *esp_zb_sleep_clusters = esp_zb_zcl_cluster_list_create();
    
    /* Basic cluster intentionally omitted for sleep config endpoint (EP3).
     * This endpoint exposes only the sleep configuration Analog Input cluster.
     */
    
    /* Create Analog Input cluster for sleep duration configuration (writable) */
    float sleep_duration_float = (float)sleep_duration_seconds;  // Convert to float for analog input
    esp_zb_analog_input_cluster_cfg_t sleep_analog_cfg = {
        .present_value = sleep_duration_float,
    };
    esp_zb_attribute_list_t *esp_zb_sleep_analog_cluster = esp_zb_analog_input_cluster_create(&sleep_analog_cfg);
    
    /* Add description attribute */
    char sleep_description[] = "\x0E""Sleep Duration";  // Length-prefixed: 14 bytes + "Sleep Duration"
    esp_zb_analog_input_cluster_add_attr(esp_zb_sleep_analog_cluster, ESP_ZB_ZCL_ATTR_ANALOG_INPUT_DESCRIPTION_ID, sleep_description);
    
    /* Add engineering units attribute (seconds) */
    uint16_t time_units = 73;  // Engineering units code for seconds
    esp_zb_analog_input_cluster_add_attr(esp_zb_sleep_analog_cluster, ESP_ZB_ZCL_ATTR_ANALOG_INPUT_ENGINEERING_UNITS_ID, &time_units);
    
    ESP_ERROR_CHECK(esp_zb_cluster_list_add_analog_input_cluster(esp_zb_sleep_clusters, esp_zb_sleep_analog_cluster, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE));
    
    /* Add Identify cluster for sleep config endpoint */
    ESP_ERROR_CHECK(esp_zb_cluster_list_add_identify_cluster(esp_zb_sleep_clusters, esp_zb_identify_cluster_create(NULL), ESP_ZB_ZCL_CLUSTER_SERVER_ROLE));
    
    esp_zb_endpoint_config_t endpoint_sleep_config = {
        .endpoint = HA_ESP_SLEEP_CONFIG_ENDPOINT,
        .app_profile_id = ESP_ZB_AF_HA_PROFILE_ID,
        .app_device_id = ESP_ZB_HA_SIMPLE_SENSOR_DEVICE_ID,
        .app_device_version = 0
    };
    esp_zb_ep_list_add_ep(esp_zb_ep_list, esp_zb_sleep_clusters, endpoint_sleep_config);

    /* LED debug endpoint removed - On/Off control moved to primary endpoint.
     * LED functionality is handled via the genOnOff cluster on the primary
     * endpoint (EP1). No separate EP4 is registered.
     */

    /* Add manufacturer info to all endpoints */
    zcl_basic_manufacturer_info_t info = {
        .manufacturer_name = ESP_MANUFACTURER_NAME,
        .model_identifier = ESP_MODEL_IDENTIFIER,
    };
    /* Add manufacturer info only to primary endpoint (BME280). Basic cluster is
     * intentionally not present on other endpoints to reduce coordinator reads.
     */
    esp_zcl_utility_add_ep_basic_manufacturer_info(esp_zb_ep_list, HA_ESP_BME280_ENDPOINT, &info);

    esp_zb_device_register(esp_zb_ep_list);
    esp_zb_core_action_handler_register(zb_action_handler);
    ESP_LOGI(TAG, "[CFG] Setting Zigbee channel mask: 0x%08lX", (unsigned long)ESP_ZB_PRIMARY_CHANNEL_MASK);
    esp_zb_set_primary_network_channel_set(ESP_ZB_PRIMARY_CHANNEL_MASK);
    ESP_ERROR_CHECK(esp_zb_start(false));
    
    /* Start BME280 periodic reading (initial delay 5 seconds) */
    esp_zb_scheduler_alarm((esp_zb_callback_t)bme280_read_and_report, 0, 5000);
    
    /* Schedule deep sleep entry - but only after network connection or extended timeout */
    if (zigbee_network_connected) {
        /* Already connected, can sleep normally after initial operations */
        esp_zb_scheduler_alarm((esp_zb_callback_t)prepare_for_deep_sleep, 0, 10000); // 10 seconds
    } else {
        /* Not connected yet, wait longer for join process and retry connection attempts */
        ESP_LOGI(TAG, "📡 Network not connected, extending wake time for join process (60 seconds)");
        esp_zb_scheduler_alarm((esp_zb_callback_t)prepare_for_deep_sleep, 0, 60000); // 60 seconds for join
        deep_sleep_scheduled = true;
    }
    
    /* Note: Button monitoring is now interrupt-based, no polling task needed */
    
    /* Start main Zigbee stack loop - will run until deep sleep scheduled task executes */
    ESP_LOGI(TAG, "⏳ Starting Zigbee stack, will enter deep sleep in 10 seconds...");
    esp_zb_stack_main_loop();
}



/* Factory reset function */
static void builtin_button_callback(button_action_t action)
{
    const char* action_names[] = {
        "none", "single", "double", "hold", "release_after_hold"
    };
    
    if (action != BUTTON_ACTION_NONE && action < (sizeof(action_names)/sizeof(action_names[0]))) {
        ESP_LOGI(TAG, "🔘 Builtin button action detected: %s", action_names[action]);
        
        if (action == BUTTON_ACTION_HOLD) {
            /* Long press detected - Factory reset */
            ESP_LOGW(TAG, "🔄 Factory reset triggered by long press - device will restart and rejoin network");
            esp_zb_scheduler_alarm((esp_zb_callback_t)factory_reset_device, 0, 100);
        }
    }
}

static void factory_reset_device(uint8_t param)
{
    ESP_LOGW(TAG, "🔄 Performing factory reset...");
    
    /* Perform factory reset - this will clear all Zigbee network settings */
    esp_zb_factory_reset();
    
    ESP_LOGI(TAG, "✅ Factory reset successful - device will restart");
    
    /* Restart the device after a short delay */
    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_restart();
}



/* BME280 sensor reading and reporting functions */
static void bme280_read_and_report(uint8_t param)
{
    float temperature, humidity, pressure;
    esp_err_t ret;
    
    // Read temperature
    ret = bme280_app_read_temperature(&temperature);
    if (ret == ESP_OK) {
        // Convert to centidegrees (Zigbee temperature unit: 0.01°C)
        int16_t temp_centidegrees = (int16_t)(temperature * 100);
        
        ret = esp_zb_zcl_set_attribute_val(HA_ESP_BME280_ENDPOINT, ESP_ZB_ZCL_CLUSTER_ID_TEMP_MEASUREMENT, 
                                          ESP_ZB_ZCL_CLUSTER_SERVER_ROLE, ESP_ZB_ZCL_ATTR_TEMP_MEASUREMENT_VALUE_ID, 
                                          &temp_centidegrees, false);
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "🌡️ Temperature: %.2f°C reported to Zigbee", temperature);
        } else {
            ESP_LOGE(TAG, "Failed to report temperature: %s", esp_err_to_name(ret));
        }
    } else {
        ESP_LOGE(TAG, "Failed to read temperature: %s", esp_err_to_name(ret));
    }
    
    // Read humidity  
    ret = bme280_app_read_humidity(&humidity);
    if (ret == ESP_OK) {
        // Convert to centipercent (Zigbee humidity unit: 0.01%)
        uint16_t hum_centipercent = (uint16_t)(humidity * 100);
        
        ret = esp_zb_zcl_set_attribute_val(HA_ESP_BME280_ENDPOINT, ESP_ZB_ZCL_CLUSTER_ID_REL_HUMIDITY_MEASUREMENT, 
                                          ESP_ZB_ZCL_CLUSTER_SERVER_ROLE, ESP_ZB_ZCL_ATTR_REL_HUMIDITY_MEASUREMENT_VALUE_ID, 
                                          &hum_centipercent, false);
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "💧 Humidity: %.2f%% reported to Zigbee", humidity);
        } else {
            ESP_LOGE(TAG, "Failed to report humidity: %s", esp_err_to_name(ret));
        }
    } else {
        ESP_LOGE(TAG, "Failed to read humidity: %s", esp_err_to_name(ret));
    }
    
    // Read pressure
    ret = bme280_app_read_pressure(&pressure);
    if (ret == ESP_OK) {
        // Convert pressure from hPa to 0.1 kPa (Zigbee pressure unit)
        // 1 hPa = 0.1 kPa, so pressure in hPa * 10 = pressure in 0.1 kPa
        int16_t pressure_zigbee = (int16_t)(pressure * 10);
        
        ret = esp_zb_zcl_set_attribute_val(HA_ESP_BME280_ENDPOINT, ESP_ZB_ZCL_CLUSTER_ID_PRESSURE_MEASUREMENT, 
                                          ESP_ZB_ZCL_CLUSTER_SERVER_ROLE, ESP_ZB_ZCL_ATTR_PRESSURE_MEASUREMENT_VALUE_ID, 
                                          &pressure_zigbee, false);
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "🌪️  Pressure: %.2f hPa (raw: %d x0.1kPa) reported to Zigbee", pressure, pressure_zigbee);
        } else {
            ESP_LOGE(TAG, "Failed to report pressure: %s", esp_err_to_name(ret));
        }
    } else {
        ESP_LOGE(TAG, "Failed to read pressure: %s", esp_err_to_name(ret));
    }
    
    // Schedule next reading (every 30 seconds)
    esp_zb_scheduler_alarm((esp_zb_callback_t)bme280_read_and_report, 0, 30000);
}

/* Rain gauge implementation */
static void IRAM_ATTR rain_gauge_isr_handler(void *arg)
{
    uint32_t gpio_num = RAIN_GAUGE_GPIO;
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    
    // ISR-safe: just increment a counter for debugging (can view in logs after interrupt)
    static uint32_t isr_trigger_count = 0;
    isr_trigger_count++;
    
    xQueueSendFromISR(rain_gauge_evt_queue, &gpio_num, &xHigherPriorityTaskWoken);
    
    if (xHigherPriorityTaskWoken) {
        portYIELD_FROM_ISR();
    }
}

static void rain_gauge_task(void *arg)
{
    uint32_t io_num;
    TickType_t last_pulse_time = 0;
    TickType_t last_debug_time = 0;
    const TickType_t DEBOUNCE_TIME = pdMS_TO_TICKS(200); // 200ms debounce - more than adequate for rain gauge
    const TickType_t BOUNCE_SETTLE_TIME = pdMS_TO_TICKS(1000); // 1 second to completely eliminate bounce issues
    const TickType_t DEBUG_INTERVAL = pdMS_TO_TICKS(5000); // Log GPIO state every 5 seconds
    
    ESP_LOGI(RAIN_TAG, "Rain gauge task started, waiting for events...");
    
    for (;;) {
        // Wait for queue with timeout to allow periodic debug logging
        if (xQueueReceive(rain_gauge_evt_queue, &io_num, pdMS_TO_TICKS(100))) {
            TickType_t current_time = xTaskGetTickCount();
            int pin_level_isr = gpio_get_level(RAIN_GAUGE_GPIO);
            
            ESP_LOGI(RAIN_TAG, "🔍 ISR TRIGGERED on GPIO%u! GPIO level: %d, enabled: %s", 
                     io_num, pin_level_isr, rain_gauge_enabled ? "YES" : "NO");
            
            // Only process if rain gauge is enabled (connected to network)
            if (!rain_gauge_enabled) {
                ESP_LOGW(RAIN_TAG, "⚠️  Rain gauge interrupt IGNORED - not connected to network");
                continue;
            }
            
            uint32_t time_diff_ms = pdTICKS_TO_MS(current_time - last_pulse_time);
            
            ESP_LOGI(RAIN_TAG, "🕐 Time since last pulse: %u ms (debounce threshold: %u ms)", 
                     time_diff_ms, pdTICKS_TO_MS(DEBOUNCE_TIME));
            
            // Reasonable debounce check - 100ms minimum between pulses
            if ((current_time - last_pulse_time) > DEBOUNCE_TIME) {
                int pin_level = gpio_get_level(RAIN_GAUGE_GPIO);
                ESP_LOGI(RAIN_TAG, "🔌 GPIO%d level: %d", RAIN_GAUGE_GPIO, pin_level);
                
                // Enhanced verification: check pin is high and wait for signal stability
                if (pin_level == 1) {
                    vTaskDelay(pdMS_TO_TICKS(20)); // Wait 20ms for signal stability
                    
                    int pin_level_after = gpio_get_level(RAIN_GAUGE_GPIO);
                    ESP_LOGI(RAIN_TAG, "🔌 GPIO%d level after 20ms: %d", RAIN_GAUGE_GPIO, pin_level_after);
                    
                    // Verify signal is still stable - for rain gauge, we want rock-solid detection
                    if (pin_level_after == 1) {
                        // Temporarily disable ISR to prevent bounce interrupts
                        ESP_LOGI(RAIN_TAG, "🔇 Disabling ISR for %u ms to prevent bounce", pdTICKS_TO_MS(BOUNCE_SETTLE_TIME));
                        gpio_isr_handler_remove(RAIN_GAUGE_GPIO);
                        
                        last_pulse_time = current_time;
                        rain_pulse_count++;
                        total_rainfall_mm += RAIN_MM_PER_PULSE;
                        
                        // Round to 2 decimal places to avoid floating-point precision accumulation
                        total_rainfall_mm = roundf(total_rainfall_mm * 100.0f) / 100.0f;
                        
                        /* Flash LED white to indicate rain pulse detected */
                        debug_led_rain_flash();
                        
                        ESP_LOGI(RAIN_TAG, "🌧️ Rain pulse #%u detected! Total: %.2f mm (+%.2f mm)", 
                                 rain_pulse_count, total_rainfall_mm, RAIN_MM_PER_PULSE);
                        
                        // Save to NVS every 10 pulses (via sleep manager for unified storage)
                        if (rain_pulse_count % 10 == 0) {
                            save_rainfall_data(total_rainfall_mm, rain_pulse_count);
                        }
                        
                        // Check if we should report to Zigbee (every hour or 1mm increment)
                        if (rain_gauge_should_report()) {
                            esp_zb_scheduler_alarm((esp_zb_callback_t)rain_gauge_zigbee_update, 0, 10);
                            last_reported_rainfall_mm = total_rainfall_mm;
                            last_report_time = current_time;
                        }
                        
                        // Wait for switch bounce to settle, then re-enable ISR
                        vTaskDelay(BOUNCE_SETTLE_TIME);
                        
                        // Re-enable ISR if still connected to network
                        if (rain_gauge_enabled) {
                            // Clear any pending interrupts before re-enabling ISR
                            gpio_intr_disable(RAIN_GAUGE_GPIO);
                            gpio_intr_enable(RAIN_GAUGE_GPIO);
                            
                            // Clear the interrupt queue of any bounce events that occurred during settle period
                            uint32_t dummy;
                            while (xQueueReceive(rain_gauge_evt_queue, &dummy, 0) == pdTRUE) {
                                // Drain any queued bounce events
                            }
                            
                            esp_err_t ret = gpio_isr_handler_add(RAIN_GAUGE_GPIO, rain_gauge_isr_handler, NULL);
                            if (ret == ESP_OK) {
                                ESP_LOGI(RAIN_TAG, "🔊 ISR re-enabled after bounce settle period (queue cleared)");
                            } else {
                                ESP_LOGE(RAIN_TAG, "❌ Failed to re-enable ISR: %s", esp_err_to_name(ret));
                            }
                        }
                    } else {
                        ESP_LOGW(RAIN_TAG, "❌ Rain pulse REJECTED - signal not stable after 20ms (bounce detected)");
                    }
                } else {
                    ESP_LOGW(RAIN_TAG, "❌ Rain pulse REJECTED - GPIO%d is LOW during interrupt (expected HIGH, check wiring!)", RAIN_GAUGE_GPIO);
                }
            } else {
                ESP_LOGW(RAIN_TAG, "⏱️ Rain pulse IGNORED - debounce protection active (%ums < %ums threshold)", 
                         time_diff_ms, pdTICKS_TO_MS(DEBOUNCE_TIME));
            }
        } else {
            // Timeout - periodic debug logging when enabled and connected
            TickType_t current_time = xTaskGetTickCount();
            if (rain_gauge_enabled && (current_time - last_debug_time) > DEBUG_INTERVAL) {
                int current_level = gpio_get_level(RAIN_GAUGE_GPIO);
                ESP_LOGI(RAIN_TAG, "🔧 Periodic check - GPIO%d level: %d, enabled: YES, total: %.2fmm", 
                         RAIN_GAUGE_GPIO, current_level, total_rainfall_mm);
                last_debug_time = current_time;
            }
        }
    }
}

static void rain_gauge_enable_isr(void)
{
    ESP_LOGI(RAIN_TAG, "🔧 Enabling rain gauge ISR on GPIO%d (installed: %s)", RAIN_GAUGE_GPIO, rain_gauge_isr_installed ? "YES" : "NO");
    
    // Set enabled flag first
    rain_gauge_enabled = true;
    
    // Add ISR handler (will succeed even if already added)
    esp_err_t ret = gpio_isr_handler_add(RAIN_GAUGE_GPIO, rain_gauge_isr_handler, NULL);
    if (ret == ESP_OK) {
        rain_gauge_isr_installed = true;
        ESP_LOGI(RAIN_TAG, "✅ Rain gauge ISR enabled successfully on GPIO%d", RAIN_GAUGE_GPIO);
    } else if (ret == ESP_ERR_INVALID_STATE) {
        // ISR already installed - this is fine
        rain_gauge_isr_installed = true;
        ESP_LOGI(RAIN_TAG, "✅ Rain gauge ISR already installed, now enabled");
    } else {
        ESP_LOGE(RAIN_TAG, "❌ Failed to enable rain gauge ISR: %s", esp_err_to_name(ret));
    }
    
    // Test GPIO level at enable time
    int current_level = gpio_get_level(RAIN_GAUGE_GPIO);
    ESP_LOGI(RAIN_TAG, "🔌 Current GPIO%d level at enable: %d", RAIN_GAUGE_GPIO, current_level);
}

static void rain_gauge_disable_isr(void)
{
    if (rain_gauge_isr_installed) {
        esp_err_t ret = gpio_isr_handler_remove(RAIN_GAUGE_GPIO);
        if (ret == ESP_OK) {
            rain_gauge_isr_installed = false;
            rain_gauge_enabled = false;
            ESP_LOGI(RAIN_TAG, "Rain gauge ISR disabled and removed");
        } else {
            ESP_LOGW(RAIN_TAG, "Failed to remove rain gauge ISR: %s", esp_err_to_name(ret));
            // Still disable processing even if removal failed
            rain_gauge_enabled = false;
        }
    } else {
        rain_gauge_enabled = false;
        ESP_LOGI(RAIN_TAG, "Rain gauge ISR already disabled");
    }
}

static bool rain_gauge_should_report(void)
{
    TickType_t current_time = xTaskGetTickCount();
    const TickType_t HOUR_MS = pdMS_TO_TICKS(3600000); // 1 hour in milliseconds
    const float REPORT_THRESHOLD_MM = 1.0f; // Report every 1mm
    
    // Check if 1 hour has passed since last report
    bool hour_passed = (current_time - last_report_time) >= HOUR_MS;
    
    // Check if we've accumulated 1mm more rainfall
    bool threshold_reached = (total_rainfall_mm - last_reported_rainfall_mm) >= REPORT_THRESHOLD_MM;
    
    if (hour_passed) {
        ESP_LOGI(RAIN_TAG, "📊 Hourly report triggered (%.2f mm total)", total_rainfall_mm);
        return true;
    } else if (threshold_reached) {
        ESP_LOGI(RAIN_TAG, "📊 Rainfall threshold report triggered (+%.2f mm, %.2f mm total)", 
                 total_rainfall_mm - last_reported_rainfall_mm, total_rainfall_mm);
        return true;
    }
    
    return false;
}

static void rain_gauge_hourly_check(uint8_t param)
{
    // This function is called every hour to ensure reporting even with very light rain
    if (rain_gauge_enabled && (total_rainfall_mm != last_reported_rainfall_mm)) {
        ESP_LOGI(RAIN_TAG, "📊 Periodic hourly check - reporting current rainfall");
        esp_zb_scheduler_alarm((esp_zb_callback_t)rain_gauge_zigbee_update, 0, 10);
        last_reported_rainfall_mm = total_rainfall_mm;
        last_report_time = xTaskGetTickCount();
    }
    
    // Debug: Check GPIO state and ISR status
    int gpio_level = gpio_get_level(RAIN_GAUGE_GPIO);
    ESP_LOGI(RAIN_TAG, "🔍 Hourly debug - GPIO%d level: %d, ISR installed: %s, enabled: %s", 
             RAIN_GAUGE_GPIO, gpio_level, rain_gauge_isr_installed ? "YES" : "NO", rain_gauge_enabled ? "YES" : "NO");
    
    // Schedule next hourly check
    esp_zb_scheduler_alarm((esp_zb_callback_t)rain_gauge_hourly_check, 0, 3600000); // 1 hour
}

static void rain_gauge_zigbee_update(uint8_t param)
{
    // Round to 2 decimal places to avoid floating-point precision issues
    float rounded_rainfall = roundf(total_rainfall_mm * 100.0f) / 100.0f;
    
    esp_err_t ret = esp_zb_zcl_set_attribute_val(HA_ESP_RAIN_GAUGE_ENDPOINT, ESP_ZB_ZCL_CLUSTER_ID_ANALOG_INPUT,
                                                 ESP_ZB_ZCL_CLUSTER_SERVER_ROLE, ESP_ZB_ZCL_ATTR_ANALOG_INPUT_PRESENT_VALUE_ID,
                                                 &rounded_rainfall, true);  // Force reporting
    
    if (ret == ESP_OK) {
        ESP_LOGI(RAIN_TAG, "✅ Rainfall total %.2f mm reported to Zigbee", rounded_rainfall);
    } else {
        ESP_LOGE(RAIN_TAG, "❌ Failed to report rainfall: %s", esp_err_to_name(ret));
    }
}

/* Battery monitoring functions */
static const char *BATTERY_TAG = "BATTERY";

#define BATTERY_ADC_CHANNEL     ADC_CHANNEL_4    // GPIO4 on ESP32-H2
#define BATTERY_ADC_UNIT        ADC_UNIT_1       // ADC1 on ESP32-H2
#define BATTERY_ADC_ATTEN       ADC_ATTEN_DB_12  // 0-3.1V range (ESP32-H2: actually 0-2.5V)

// For Li-Ion battery monitoring via voltage divider:
// Hardware: R1=99kΩ, R2=99.1kΩ → theoretical divider = 2.0
// However, ESP32-H2 ADC calibration has issues with DB_12 attenuation
// Empirical calibration: ADC reads 1300mV when actual is 2085mV
// Correction factor: 2085/1300 = 1.604
#define BATTERY_VOLTAGE_DIVIDER 2.0f             // Hardware divider (R1≈R2≈100kΩ)
#define BATTERY_ADC_CORRECTION  1.604f           // ADC calibration correction for ESP32-H2
#define BATTERY_MIN_VOLTAGE     2.7f             // Li-Ion minimum safe voltage (V)
#define BATTERY_MAX_VOLTAGE     4.2f             // Li-Ion maximum voltage (V)

// ADC handles
static adc_oneshot_unit_handle_t adc_handle = NULL;
static adc_cali_handle_t adc_cali_handle = NULL;

// Initialize ADC for battery monitoring
static esp_err_t battery_adc_init(void)
{
    // Configure ADC1
    adc_oneshot_unit_init_cfg_t init_config = {
        .unit_id = BATTERY_ADC_UNIT,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };
    esp_err_t ret = adc_oneshot_new_unit(&init_config, &adc_handle);
    if (ret == ESP_ERR_NOT_FOUND || ret == ESP_ERR_INVALID_STATE) {
        // ADC already initialized - try to continue without creating new unit
        ESP_LOGW(BATTERY_TAG, "ADC1 already in use, attempting to use without calibration");
        adc_handle = NULL;  // Will use fallback in battery_read_and_report
        return ESP_OK;  // Don't fail, just use simulated values
    } else if (ret != ESP_OK) {
        ESP_LOGE(BATTERY_TAG, "Failed to initialize ADC unit: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Configure ADC channel
    adc_oneshot_chan_cfg_t chan_config = {
        .atten = BATTERY_ADC_ATTEN,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    ret = adc_oneshot_config_channel(adc_handle, BATTERY_ADC_CHANNEL, &chan_config);
    if (ret != ESP_OK) {
        ESP_LOGE(BATTERY_TAG, "Failed to configure ADC channel: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Initialize ADC calibration
#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    adc_cali_curve_fitting_config_t cali_config = {
        .unit_id = BATTERY_ADC_UNIT,
        .atten = BATTERY_ADC_ATTEN,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    ret = adc_cali_create_scheme_curve_fitting(&cali_config, &adc_cali_handle);
    if (ret == ESP_OK) {
        ESP_LOGI(BATTERY_TAG, "ADC calibration scheme: Curve Fitting");
    }
#elif ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
    adc_cali_line_fitting_config_t cali_config = {
        .unit_id = BATTERY_ADC_UNIT,
        .atten = BATTERY_ADC_ATTEN,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    ret = adc_cali_create_scheme_line_fitting(&cali_config, &adc_cali_handle);
    if (ret == ESP_OK) {
        ESP_LOGI(BATTERY_TAG, "ADC calibration scheme: Line Fitting");
    }
#endif
    
    if (ret != ESP_OK) {
        ESP_LOGW(BATTERY_TAG, "ADC calibration failed, using raw values: %s", esp_err_to_name(ret));
        adc_cali_handle = NULL;
    }
    
    ESP_LOGI(BATTERY_TAG, "✅ Battery ADC initialized on GPIO4 (ADC1_CH4)");
    return ESP_OK;
}

static void battery_read_and_report(uint8_t param)
{
    ESP_LOGI(BATTERY_TAG, "🔧 battery_read_and_report() called");
    
    /* Power optimization: Read battery only once per hour (time-based) to save ~360µAh/day
     * This ensures we read once per hour regardless of wake reason (rain vs timer).
     * Uses NVS to persist timestamp across deep sleep. */
    
    const uint32_t BATTERY_READ_INTERVAL_SEC = 3600;  // 1 hour
    
    // Get current time since boot (in seconds)
    uint32_t current_time_sec = (uint32_t)(esp_timer_get_time() / 1000000ULL);
    
    ESP_LOGI(BATTERY_TAG, "🕐 Current time since boot: %lu seconds", current_time_sec);
    
    // Read last battery reading timestamp from NVS
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open("storage", NVS_READONLY, &nvs_handle);
    uint32_t last_battery_read_time = 0;
    bool first_reading = false;
    bool force_read = false;
    
    if (err == ESP_OK) {
        err = nvs_get_u32(nvs_handle, "batt_time", &last_battery_read_time);
        if (err != ESP_OK) {
            first_reading = true;  // Key doesn't exist - this is the first reading
            ESP_LOGI(BATTERY_TAG, "📝 No previous battery timestamp in NVS - first reading");
        } else {
            ESP_LOGI(BATTERY_TAG, "📝 Last battery timestamp from NVS: %lu seconds", last_battery_read_time);
            
            // Check for reboot: if saved timestamp is close to current boot time (within 30 seconds),
            // it means we rebooted and should force a reading
            if (last_battery_read_time < 30 && current_time_sec < 30) {
                ESP_LOGI(BATTERY_TAG, "🔄 Recent boot detected (both times < 30s) - forcing battery read");
                force_read = true;
            }
        }
        nvs_close(nvs_handle);
    } else {
        first_reading = true;  // NVS not available - assume first reading
        ESP_LOGW(BATTERY_TAG, "⚠️  NVS not available - assuming first reading");
    }
    
    // Check if enough time has elapsed (skip interval check on first reading or forced read)
    if (!first_reading && !force_read) {
        // Handle potential timer overflow on reboot: if last_battery_read_time is much larger
        // than current_time_sec, it means we rebooted and should read battery
        if (last_battery_read_time > current_time_sec) {
            ESP_LOGI(BATTERY_TAG, "🔄 Device rebooted (timer reset detected) - forcing battery read");
            force_read = true;
        } else {
            uint32_t elapsed_sec = current_time_sec - last_battery_read_time;
            
            ESP_LOGI(BATTERY_TAG, "⏱️  Elapsed time: %lu seconds (need %lu for next reading)", 
                     elapsed_sec, BATTERY_READ_INTERVAL_SEC);
            
            if (elapsed_sec < BATTERY_READ_INTERVAL_SEC) {
                ESP_LOGI(BATTERY_TAG, "⏭️  Skipping battery read (%lu sec since last, need %lu)", 
                         elapsed_sec, BATTERY_READ_INTERVAL_SEC);
                return;  // Skip this reading
            }
            
            ESP_LOGI(BATTERY_TAG, "🔋 Reading battery (last read %lu sec ago)", elapsed_sec);
        }
    }
    
    if (first_reading) {
        ESP_LOGI(BATTERY_TAG, "🔋 Reading battery (first reading after boot/pairing)");
    } else if (force_read) {
        ESP_LOGI(BATTERY_TAG, "🔋 Reading battery (forced after reboot)");
    }
    
    // Time to read battery - update timestamp in NVS
    err = nvs_open("storage", NVS_READWRITE, &nvs_handle);
    if (err == ESP_OK) {
        nvs_set_u32(nvs_handle, "batt_time", current_time_sec);
        nvs_commit(nvs_handle);
        nvs_close(nvs_handle);
    }
    
    float battery_voltage = 0.0f;
    
    if (adc_handle == NULL) {
        ESP_LOGE(BATTERY_TAG, "ADC not initialized, using simulated value");
        battery_voltage = 3.7f;  // Fallback simulated value
    } else {
    /* Power optimization: Reduced from 10 to 3 samples (better accuracy, still low power) */
    const int num_samples = 3;
        int voltage_sum = 0;
        int raw_sum = 0;
        
        for (int i = 0; i < num_samples; i++) {
            int adc_raw;
            esp_err_t ret = adc_oneshot_read(adc_handle, BATTERY_ADC_CHANNEL, &adc_raw);
            if (ret != ESP_OK) {
                ESP_LOGE(BATTERY_TAG, "ADC read failed: %s", esp_err_to_name(ret));
                battery_voltage = 3.7f;  // Fallback value
                goto skip_adc;
            }
            
            raw_sum += adc_raw;  // Track raw ADC values for debugging
            
            int voltage_mv;
            if (adc_cali_handle != NULL) {
                ret = adc_cali_raw_to_voltage(adc_cali_handle, adc_raw, &voltage_mv);
                if (ret != ESP_OK) {
                    ESP_LOGE(BATTERY_TAG, "ADC calibration failed: %s", esp_err_to_name(ret));
                    // Fallback: rough calculation
                    voltage_mv = (adc_raw * 2500) / 4095;
                }
                // Apply ESP32-H2 ADC correction factor (empirically determined)
                voltage_mv = (int)((float)voltage_mv * BATTERY_ADC_CORRECTION);
            } else {
                // No calibration available
                voltage_mv = (adc_raw * 2500) / 4095;
                voltage_mv = (int)((float)voltage_mv * BATTERY_ADC_CORRECTION);
            }
            
            voltage_sum += voltage_mv;
            /* Power optimization: Removed 10ms delay - ADC can sample back-to-back (saves ~90% overhead) */
        }
        
        // Calculate average voltage at ADC input (in volts)
        float adc_voltage = (voltage_sum / num_samples) / 1000.0f;
        int avg_raw = raw_sum / num_samples;
        
        // Apply voltage divider multiplier to get actual battery voltage
        battery_voltage = adc_voltage * BATTERY_VOLTAGE_DIVIDER;
        
        ESP_LOGI(BATTERY_TAG, "📊 ADC raw avg: %d, calibrated: %dmV (%.3fV) → Battery: %.2fV", 
                 avg_raw, voltage_sum / num_samples, adc_voltage, battery_voltage);
    }
    
skip_adc:
    
    // Calculate battery percentage (0-100%) using Li-Ion discharge curve
    // Li-Ion voltage curve is fairly linear between 3.0V-4.2V
    float percentage = ((battery_voltage - BATTERY_MIN_VOLTAGE) / (BATTERY_MAX_VOLTAGE - BATTERY_MIN_VOLTAGE)) * 100.0f;
    if (percentage > 100.0f) percentage = 100.0f;
    if (percentage < 0.0f) percentage = 0.0f;
    
    // Zigbee uses different units:
    // - Battery voltage: 0.1V units (e.g., 30 = 3.0V)
    // - Battery percentage: 0-200 scale (200 = 100%, 100 = 50%)
    uint8_t zigbee_voltage = (uint8_t)(battery_voltage * 10.0f);
    uint8_t zigbee_percentage = (uint8_t)(percentage * 2.0f);
    
    // Update battery voltage attribute (0x0020)
    esp_err_t ret = esp_zb_zcl_set_attribute_val(
        HA_ESP_BME280_ENDPOINT,
        ESP_ZB_ZCL_CLUSTER_ID_POWER_CONFIG,
        ESP_ZB_ZCL_CLUSTER_SERVER_ROLE,
        0x0020,  // Battery Voltage attribute ID
        &zigbee_voltage,
        false  // Don't force report, let reporting configuration handle it
    );
    
    if (ret != ESP_OK) {
        ESP_LOGE(BATTERY_TAG, "❌ Failed to update battery voltage: %s", esp_err_to_name(ret));
    }
    
    // Update battery percentage attribute (0x0021)
    ret = esp_zb_zcl_set_attribute_val(
        HA_ESP_BME280_ENDPOINT,
        ESP_ZB_ZCL_CLUSTER_ID_POWER_CONFIG,
        ESP_ZB_ZCL_CLUSTER_SERVER_ROLE,
        0x0021,  // Battery Percentage Remaining attribute ID
        &zigbee_percentage,
        false
    );
    
    if (ret != ESP_OK) {
        ESP_LOGE(BATTERY_TAG, "❌ Failed to update battery percentage: %s", esp_err_to_name(ret));
    }
    
    ESP_LOGI(BATTERY_TAG, "🔋 Li-Ion Battery: %.2fV (%.0f%%) - Zigbee values: %u (0.1V), %u (%%*2)", 
             battery_voltage, percentage, zigbee_voltage, zigbee_percentage);
}

/* Sleep duration reporting function */
static void sleep_duration_report(uint8_t param)
{
    float sleep_duration_float = (float)sleep_duration_seconds;
    
    esp_err_t ret = esp_zb_zcl_set_attribute_val(
        HA_ESP_SLEEP_CONFIG_ENDPOINT,
        ESP_ZB_ZCL_CLUSTER_ID_ANALOG_INPUT,
        ESP_ZB_ZCL_CLUSTER_SERVER_ROLE,
        ESP_ZB_ZCL_ATTR_ANALOG_INPUT_PRESENT_VALUE_ID,
        &sleep_duration_float,
        true  // Force reporting to ensure Z2M gets the update
    );
    
    if (ret == ESP_OK) {
        ESP_LOGI(SLEEP_CONFIG_TAG, "✅ Sleep duration %.0f seconds (%.1f min) reported to Zigbee", 
                 sleep_duration_float, sleep_duration_float / 60.0f);
    } else {
        ESP_LOGE(SLEEP_CONFIG_TAG, "❌ Failed to report sleep duration: %s", esp_err_to_name(ret));
    }
}

/* Sleep configuration functions */
static esp_err_t sleep_config_load(void)
{
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(SLEEP_CONFIG_NVS_NAMESPACE, NVS_READONLY, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGI(SLEEP_CONFIG_TAG, "No previous sleep configuration found, using default %d seconds", sleep_duration_seconds);
        return ESP_OK;
    }

    size_t required_size = sizeof(uint32_t);
    err = nvs_get_blob(nvs_handle, SLEEP_CONFIG_NVS_KEY, &sleep_duration_seconds, &required_size);
    if (err == ESP_OK) {
        // Validate range: 60 seconds (1 minute) to 7200 seconds (2 hours)
        if (sleep_duration_seconds < 60) {
            ESP_LOGW(SLEEP_CONFIG_TAG, "Sleep duration too short (%d), setting to minimum 60 seconds", sleep_duration_seconds);
            sleep_duration_seconds = 60;
        } else if (sleep_duration_seconds > 7200) {
            ESP_LOGW(SLEEP_CONFIG_TAG, "Sleep duration too long (%d), setting to maximum 7200 seconds", sleep_duration_seconds);
            sleep_duration_seconds = 7200;
        }
        ESP_LOGI(SLEEP_CONFIG_TAG, "📂 Loaded sleep duration: %d seconds (%.1f minutes)", 
                 sleep_duration_seconds, sleep_duration_seconds / 60.0f);
    } else {
        ESP_LOGI(SLEEP_CONFIG_TAG, "No sleep configuration found, using default %d seconds", sleep_duration_seconds);
    }

    nvs_close(nvs_handle);
    return ESP_OK;
}

static esp_err_t sleep_config_save(void)
{
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(SLEEP_CONFIG_NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(SLEEP_CONFIG_TAG, "Failed to open NVS for sleep config: %s", esp_err_to_name(err));
        return err;
    }

    err = nvs_set_blob(nvs_handle, SLEEP_CONFIG_NVS_KEY, &sleep_duration_seconds, sizeof(uint32_t));
    if (err == ESP_OK) {
        err = nvs_commit(nvs_handle);
        if (err == ESP_OK) {
            ESP_LOGI(SLEEP_CONFIG_TAG, "💾 Sleep duration saved: %d seconds (%.1f minutes)", 
                     sleep_duration_seconds, sleep_duration_seconds / 60.0f);
        }
    } else {
        ESP_LOGE(SLEEP_CONFIG_TAG, "Failed to save sleep duration: %s", esp_err_to_name(err));
    }

    nvs_close(nvs_handle);
    return err;
}

static void rain_gauge_init(void)
{
    ESP_LOGI(RAIN_TAG, "Initializing rain gauge on GPIO%d (disabled until network connection)", RAIN_GAUGE_GPIO);
    
    /* Start disabled - will be enabled when connected to network */
    rain_gauge_enabled = false;
    
    /* Load previous rainfall total from sleep manager (which handles both RTC and NVS) */
    float loaded_rainfall = 0.0f;
    uint32_t loaded_pulses = 0;
    load_rainfall_data(&loaded_rainfall, &loaded_pulses);
    
    // Round loaded value to 2 decimal places to avoid floating-point precision issues
    total_rainfall_mm = roundf(loaded_rainfall * 100.0f) / 100.0f;
    rain_pulse_count = loaded_pulses;
    
    if (loaded_rainfall > 0.0f || loaded_pulses > 0) {
        ESP_LOGI(RAIN_TAG, "Loaded previous rainfall total: %.2f mm (%lu pulses)", total_rainfall_mm, rain_pulse_count);
    } else {
        ESP_LOGI(RAIN_TAG, "No previous rainfall data found, starting from 0.0mm");
    }
    
    /* Handle rain wake-up: count the pulse that caused the wake-up from deep sleep */
    wake_reason_t wake_reason = check_wake_reason();
    if (wake_reason == WAKE_REASON_RAIN) {
        rain_pulse_count++;
        total_rainfall_mm += RAIN_MM_PER_PULSE;
        total_rainfall_mm = roundf(total_rainfall_mm * 100.0f) / 100.0f; // Round to 2 decimals
        
        ESP_LOGI(RAIN_TAG, "🌧️ Rain pulse detected during sleep! Pulse #%u, Total: %.2f mm (+%.2f mm)", 
                 rain_pulse_count, total_rainfall_mm, RAIN_MM_PER_PULSE);
        
        // Save to NVS immediately when waking from rain
        save_rainfall_data(total_rainfall_mm, rain_pulse_count);
        
        // Mark that we should report this on next Zigbee connection
        last_reported_rainfall_mm = total_rainfall_mm - 1.0f; // Force threshold reporting
    }
    
    /* Configure GPIO for rain gauge */
    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_POSEDGE,  // Interrupt on rising edge
        .pin_bit_mask = (1ULL << RAIN_GAUGE_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = 0,   // No pull-up needed
        .pull_down_en = 1, // Enable internal pull-down for additional noise protection
    };
    esp_err_t ret = gpio_config(&io_conf);
    if (ret != ESP_OK) {
        ESP_LOGE(RAIN_TAG, "Failed to configure GPIO%d: %s", RAIN_GAUGE_GPIO, esp_err_to_name(ret));
        return;
    }
    
    ESP_LOGI(RAIN_TAG, "✅ GPIO%d configured successfully", RAIN_GAUGE_GPIO);
    
    // Check initial GPIO state
    int initial_level = gpio_get_level(RAIN_GAUGE_GPIO);
    ESP_LOGI(RAIN_TAG, "🔌 Initial GPIO%d level: %d (with pull-down)", RAIN_GAUGE_GPIO, initial_level);
    
    /* Create queue for GPIO events */
    rain_gauge_evt_queue = xQueueCreate(10, sizeof(uint32_t));
    if (!rain_gauge_evt_queue) {
        ESP_LOGE(RAIN_TAG, "Failed to create event queue");
        return;
    }
    
    /* Install GPIO ISR service if not already installed */
    esp_err_t isr_ret = gpio_install_isr_service(ESP_INTR_FLAG_DEFAULT);
    if (isr_ret == ESP_ERR_INVALID_STATE) {
        ESP_LOGD(RAIN_TAG, "GPIO ISR service already installed (shared with button driver)");
    } else if (isr_ret != ESP_OK) {
        ESP_LOGE(RAIN_TAG, "Failed to install ISR service: %s", esp_err_to_name(isr_ret));
        return;
    } else {
        ESP_LOGD(RAIN_TAG, "GPIO ISR service installed successfully");
    }
    
    /* Don't add ISR handler yet - will be added when network connects */
    ESP_LOGI(RAIN_TAG, "Rain gauge GPIO configured, ISR will be enabled when connected to network");
    
    /* Create rain gauge task */
    BaseType_t task_ret = xTaskCreate(rain_gauge_task, "rain_gauge_task", 2048, NULL, 5, NULL);
    if (task_ret != pdPASS) {
        ESP_LOGE(RAIN_TAG, "Failed to create rain gauge task");
        return;
    }
    
    /* Initialize reporting variables */
    last_reported_rainfall_mm = total_rainfall_mm;
    last_report_time = xTaskGetTickCount();
    
    /* Add continuous GPIO monitoring for debugging */
    ESP_LOGI(RAIN_TAG, "Rain gauge initialized successfully. Current total: %.2f mm", total_rainfall_mm);
    ESP_LOGI(RAIN_TAG, "🔧 GPIO%d monitoring: level=%d, pull-down=enabled, trigger=RISING_EDGE", 
             RAIN_GAUGE_GPIO, gpio_get_level(RAIN_GAUGE_GPIO));
    
    /* Report initial value to Zigbee after device joins network */
    esp_zb_scheduler_alarm((esp_zb_callback_t)rain_gauge_zigbee_update, 0, 10000); // 10 seconds delay
    
    /* Start hourly reporting timer */
    esp_zb_scheduler_alarm((esp_zb_callback_t)rain_gauge_hourly_check, 0, 3600000); // First check in 1 hour
    
    /* Add a more frequent debug check initially (every 30 seconds for debugging) */
    esp_zb_scheduler_alarm((esp_zb_callback_t)rain_gauge_hourly_check, 0, 30000); // First debug check in 30 seconds
}

void app_main(void)
{
    /* Initialize NVS */
    ESP_ERROR_CHECK(nvs_flash_init());
    
    /* Initialize debug LED */
    debug_led_init();
    
    /* Set initial LED state - will show status immediately on battery power */
    if (led_debug_enabled) {
        if (zigbee_network_connected) {
            debug_led_set(true);  // Blue - already connected
            ESP_LOGI(TAG, "💡 LED initialized: BLUE (network connected)");
        } else {
            debug_led_start_blink();  // Orange blink - will join network
            ESP_LOGI(TAG, "💡 LED initialized: ORANGE BLINK (joining network)");
        }
    }
    
    /* Initialize OTA */
    ESP_ERROR_CHECK(esp_zb_ota_init());
    
    /* Check wake-up reason and print statistics */
    wake_reason_t wake_reason = check_wake_reason();
    print_wake_statistics();
    
    /* Load rainfall data from RTC/NVS */
    float rainfall_mm = 0.0f;
    uint32_t pulse_count = 0;
    load_rainfall_data(&rainfall_mm, &pulse_count);
    
    /* Print battery life estimate (assuming 2500mAh battery) */
    estimate_battery_life(2500);
    
    /* Configure ESP-IDF platform */
    esp_zb_platform_config_t config = {
        .radio_config = ESP_ZB_DEFAULT_RADIO_CONFIG(),
        .host_config = ESP_ZB_DEFAULT_HOST_CONFIG(),
    };
    ESP_ERROR_CHECK(esp_zb_platform_config(&config));
    
    /* Start Zigbee task */
    ESP_LOGI(TAG, "🚀 Starting Caelum Weather Station (Battery Mode)");
    
    /* Log firmware version from app description */
    esp_app_desc_t app_desc_main;
    const esp_partition_t *running = esp_ota_get_running_partition();
    if (esp_ota_get_partition_description(running, &app_desc_main) == ESP_OK) {
        ESP_LOGI(TAG, "📦 Firmware: %s (Date: %s %s)", app_desc_main.version, app_desc_main.date, app_desc_main.time);
    }
    
#ifdef OTA_FILE_VERSION
    ESP_LOGI(TAG, "⚙️  OTA: Version=0x%08lX, Manufacturer=0x%04X, ImageType=0x%04X", 
             (unsigned long)OTA_FILE_VERSION, OTA_UPGRADE_MANUFACTURER, OTA_UPGRADE_IMAGE_TYPE);
#else
    ESP_LOGI(TAG, "⚙️  OTA: Manufacturer=0x%04X, ImageType=0x%04X", OTA_UPGRADE_MANUFACTURER, OTA_UPGRADE_IMAGE_TYPE);
#endif
    ESP_LOGI(TAG, "Wake reason: %s", 
             wake_reason == WAKE_REASON_TIMER ? "TIMER" :
             wake_reason == WAKE_REASON_RAIN ? "RAIN" :
             wake_reason == WAKE_REASON_BUTTON ? "BUTTON" : "RESET");
    
    /* Log network connection status */
    if (zigbee_network_connected) {
        ESP_LOGI(TAG, "📡 Network: Connected");
    } else {
        ESP_LOGW(TAG, "📡 Network: Disconnected (retries: %d/%d)", connection_retry_count, MAX_CONNECTION_RETRIES);
    }
    
    xTaskCreate(esp_zb_task, "Zigbee_main", 4096, NULL, 5, NULL);
}
