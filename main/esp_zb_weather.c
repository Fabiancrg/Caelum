/*
 * SPDX-FileCopyrightText: 2021-2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier:  LicenseRef-Included
 *
 * ESP32-H2 Zigbee Weather Station with Light Sleep
 *
 * Battery-powered weather station with 15-minute wake intervals
 * and immediate wake-up on rain detection (>1mm)
 * Uses light sleep to maintain Zigbee network connection for faster response
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
#include "esp_pm.h"
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

/* Zigbee Sleepy End Device (SED) configuration */
#define ZIGBEE_KEEP_ALIVE_MS        7500    // Keep-alive poll interval (7.5 seconds)
#define ZIGBEE_SLEEP_THRESHOLD_MS   6000    // Idle time before sleep signal (6 seconds)
#define ZIGBEE_ED_TIMEOUT           ESP_ZB_ED_AGING_TIMEOUT_64MIN  // Parent timeout

/* Power optimization notes:
 * - Sleep threshold MUST be < keep_alive to allow CAN_SLEEP signal before next poll
 * - Lower threshold = faster sleep entry = better battery life
 * - Higher threshold = more time for multiple reports = fewer wake cycles
 * - Current setting (6s): Optimal for 15-min reporting with occasional rain events
 * - Measured performance: 0.68mA sleep, 12mA transmit, ~0.83mA average
 * - Battery life: ~125 days (4 months) on 2500mAh Li-Ion
 */

static const char *TAG = "WEATHER_STATION";
//static const char *RAIN_TAG = "RAIN_GAUGE";

/* Network connection status - declared early for LED functions */
static bool zigbee_network_connected = false;

/* LED is used only during boot/join process:
 * - Blink yellow/orange during network joining
 * - Steady blue when successfully connected
 * - Blink red after max connection retries failed
 * After successful join, LED strip is deinitialized to save power */

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

static void debug_led_deinit(void)
{
    /* Deinitialize LED strip to power down RMT peripheral
     * Called after successful network join to save power */
    if (led_strip) {
        led_strip_clear(led_strip);  // Clear pixels first
        led_strip_del(led_strip);    // Delete LED strip handle (powers down RMT)
        led_strip = NULL;
        ESP_LOGI(TAG, "🔌 RGB LED RMT peripheral powered down (boot sequence complete)");
    }
}

static void debug_led_set_blue(void)
{
    /* Set steady blue for successful connection */
    if (led_strip) {
        led_strip_set_pixel(led_strip, 0, 0, 0, 16);  // R=0, G=0, B=16 (dim blue)
        led_strip_refresh(led_strip);
    }
}

static void debug_led_blink_task(void *arg)
{
    while (led_blink_task_running) {
        if (!led_strip) break;  // Stop if LED was deinitialized
        
        /* Yellow/orange blink for network joining */
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
    if (!led_strip) return;  // LED already deinitialized
    
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

static void debug_led_blink_red(void)
{
    /* Blink red to indicate connection failure after max retries */
    if (!led_strip) return;
    
    for (int i = 0; i < 10; i++) {  // Blink 10 times
        led_strip_set_pixel(led_strip, 0, 16, 0, 0);  // R=16, G=0, B=0 (dim red)
        led_strip_refresh(led_strip);
        vTaskDelay(pdMS_TO_TICKS(200));  // ON for 200ms
        
        led_strip_set_pixel(led_strip, 0, 0, 0, 0);   // OFF
        led_strip_refresh(led_strip);
        vTaskDelay(pdMS_TO_TICKS(200));  // OFF for 200ms
    }
}

/* Rain gauge configuration */
#define RAIN_GAUGE_GPIO         12              // GPIO pin for rain gauge reed switch
#define RAIN_MM_PER_PULSE       0.36f           // mm of rain per bucket tip (adjust for your sensor)

/* Rain gauge variables */
typedef struct {
    uint32_t gpio_num;
    TickType_t tick;
} rain_evt_t;

static QueueHandle_t rain_gauge_evt_queue = NULL;
static float total_rainfall_mm = 0.0f;
static uint32_t rain_pulse_count = 0;
static const char *RAIN_TAG = "RAIN_GAUGE";
static bool rain_gauge_enabled = false;  // Only enable when connected to network
static bool rain_gauge_isr_installed = false;  // Track ISR installation state
static float last_reported_rainfall_mm = 0.0f;  // Track last reported value for 1mm threshold

/* Rain reporting thresholds */
#define REPORT_THRESHOLD_MM 1.0f  // Report every 1mm of rainfall

/* Periodic reporting timer */
static esp_timer_handle_t periodic_report_timer = NULL;
#define PERIODIC_READING_INTERVAL_MS (SLEEP_DURATION_MINUTES * 60 * 1000)  // 15 minutes in milliseconds

/* Network connection status (zigbee_network_connected declared earlier for LED functions) */
static uint32_t connection_retry_count = 0;
#define NETWORK_RETRY_SLEEP_DURATION    30      // 30 seconds for network retry
#define MAX_CONNECTION_RETRIES          20      // Max retries before giving up (10 minutes total)

/* Button action tracking (no state needed for action-based buttons) */

/********************* Define functions **************************/
static void builtin_button_callback(button_action_t action);
static void factory_reset_device(uint8_t param);
static void bme280_read_and_report(uint8_t param);
static void periodic_sensor_report_callback(void *arg);
static void start_periodic_reading(void);
static void stop_periodic_reading(void);
static void rain_gauge_init(void);
static void rain_gauge_isr_handler(void *arg);
static void rain_gauge_task(void *arg);
static void rain_gauge_zigbee_update(uint8_t param);
static void rain_gauge_enable_isr(void);
static void rain_gauge_disable_isr(void);
static esp_err_t battery_adc_init(void);
static void battery_read_and_report(uint8_t param);
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
    
    return ESP_OK;
}

/**
 * @brief Initialize power management for light sleep
 */
static esp_err_t esp_zb_power_save_init(void)
{
    esp_err_t rc = ESP_OK;
#ifdef CONFIG_PM_ENABLE
    int cur_cpu_freq_mhz = CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ;
    esp_pm_config_t pm_config = {
        .max_freq_mhz = cur_cpu_freq_mhz,
        .min_freq_mhz = cur_cpu_freq_mhz,
#if CONFIG_FREERTOS_USE_TICKLESS_IDLE
        .light_sleep_enable = true
#endif
    };
    rc = esp_pm_configure(&pm_config);
    ESP_LOGI(TAG, "Power management configured: max=%dMHz, min=%dMHz, light_sleep=%s",
             cur_cpu_freq_mhz, cur_cpu_freq_mhz, 
#if CONFIG_FREERTOS_USE_TICKLESS_IDLE
             "enabled"
#else
             "disabled"
#endif
    );
#else
    ESP_LOGW(TAG, "CONFIG_PM_ENABLE not set - light sleep may not work properly");
#endif
    return rc;
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
        /* LED is already deinitialized after initial join, no action needed */
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
                ESP_LOGI(TAG, "Device rebooted - rejoining previous network");
                debug_led_start_blink();  // Start blinking when rejoining
                
                /* CRITICAL: Device must rejoin network after reboot!
                 * Don't mark as connected or schedule reports yet - wait for ESP_ZB_BDB_SIGNAL_STEERING success.
                 * The ESP_ZB_BDB_SIGNAL_STEERING handler will enable rain gauge and schedule initial reports. */
                esp_zb_bdb_start_top_level_commissioning(ESP_ZB_BDB_MODE_NETWORK_STEERING);
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
            
            debug_led_stop_blink();       // Stop blinking
            debug_led_set_blue();         // Set steady blue to indicate success
            
            /* Mark network as connected and reset retry count */
            zigbee_network_connected = true;
            connection_retry_count = 0;
            
            /* Enable rain gauge now that we're connected */
            rain_gauge_enable_isr();
            ESP_LOGI(RAIN_TAG, "Rain gauge enabled - device connected to Zigbee network");
            
            /* Schedule sensor data reporting after first connection */
            ESP_LOGI(TAG, "📊 Scheduling initial sensor data reporting after network join");
            esp_zb_scheduler_alarm((esp_zb_callback_t)bme280_read_and_report, 0, 1000); // Report in 1 second
            esp_zb_scheduler_alarm((esp_zb_callback_t)rain_gauge_zigbee_update, 0, 2000); // Report rainfall in 2 seconds
            esp_zb_scheduler_alarm((esp_zb_callback_t)battery_read_and_report, 0, 3000); // Report battery in 3 seconds
            
            /* Start periodic sensor reading timer for 15-minute intervals.
             * This ensures sensors are read regularly and attributes stay updated.
             * Actual reporting to coordinator is controlled by Zigbee reporting configuration. */
            start_periodic_reading();
            
            /* Deinitialize LED after successful join - LED kept on for 5 seconds to confirm join, then powered down */
            ESP_LOGI(TAG, "💡 LED will power down in 5 seconds to save battery");
            esp_zb_scheduler_alarm((esp_zb_callback_t)debug_led_deinit, 0, 5000); // Power down LED in 5 seconds
            
            /* Device will enter light sleep automatically when all initial reports complete */
            ESP_LOGI(TAG, "💤 Initial reports scheduled - device will sleep when idle");
        } else {
            ESP_LOGI(TAG, "Network steering was not successful (status: %s)", esp_err_to_name(err_status));
            
            /* Mark network as disconnected and increment retry count */
            zigbee_network_connected = false;
            connection_retry_count++;
            
            /* Disable rain gauge when not connected */
            rain_gauge_disable_isr();
            ESP_LOGW(RAIN_TAG, "Rain gauge disabled - not connected to network");
            
            /* Stop periodic sensor reading timer when disconnected */
            stop_periodic_reading();
            
            /* Check if max retries reached */
            if (connection_retry_count >= MAX_CONNECTION_RETRIES) {
                ESP_LOGE(TAG, "❌ Max connection retries (%d) reached - giving up", MAX_CONNECTION_RETRIES);
                debug_led_stop_blink();
                debug_led_blink_red();  // Blink red to indicate failure
                /* LED will be deinitialized after red blink sequence completes */
                esp_zb_scheduler_alarm((esp_zb_callback_t)debug_led_deinit, 0, 5000);
                /* Device will continue to operate but won't retry network join */
                break;
            }
            
            ESP_LOGW(TAG, "🔄 Connection attempt %d/%d failed - retrying", connection_retry_count, MAX_CONNECTION_RETRIES);
            esp_zb_scheduler_alarm((esp_zb_callback_t)bdb_start_top_level_commissioning_cb, ESP_ZB_BDB_MODE_NETWORK_STEERING, 1000);
        }
        break;
    case ESP_ZB_COMMON_SIGNAL_CAN_SLEEP:
        /* Check if OTA upgrade is in progress - MUST NOT sleep during OTA! */
        if (esp_zb_ota_is_active()) {
            ESP_LOGW(TAG, "⚠️ OTA upgrade in progress - preventing sleep");
            /* Don't call esp_zb_sleep_now() - let OTA complete */
            break;
        }
        
        ESP_LOGI(TAG, "Zigbee can sleep");
        /* LED is already deinitialized after successful join - no action needed */
        esp_zb_sleep_now();
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
    
    /* No custom attribute handling needed - all attributes managed by standard Zigbee reporting */
    
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
 * @brief Prepare for light sleep - called by Zigbee scheduler after operations complete
 */
/* NOTE: prepare_for_light_sleep() and sed_wake_and_report() functions removed.
 * For Sleepy End Device (SED) mode with automatic sleep:
 * - DO NOT use scheduler alarms for continuous operations
 * - The Zigbee stack handles sleep/wake automatically via ESP_ZB_COMMON_SIGNAL_CAN_SLEEP
 * - Device wakes naturally every keep_alive interval (7.5s) to poll parent
 * - Continuous alarms prevent the CAN_SLEEP signal from ever triggering
 * - Reports are now triggered on-demand or by external events (rain gauge, etc)
 */

// Helper to fill a Zigbee ZCL string (first byte = length, then chars)
static void fill_zcl_string(char *buf, size_t bufsize, const char *src) {
    size_t len = strlen(src);
    if (len > bufsize - 1) len = bufsize - 1; // Reserve 1 byte for length
    buf[0] = (uint8_t)len;
    memcpy(&buf[1], src, len);
}

static void esp_zb_task(void *pvParameters)
{
    // Initialize Zigbee stack as Sleepy End Device (SED)
    esp_zb_cfg_t zb_nwk_cfg = ESP_ZB_ZED_CONFIG();
    
    // Configure as Sleepy End Device for low power operation 
    zb_nwk_cfg.nwk_cfg.zed_cfg.ed_timeout = ZIGBEE_ED_TIMEOUT;
    zb_nwk_cfg.nwk_cfg.zed_cfg.keep_alive = ZIGBEE_KEEP_ALIVE_MS;
    
    /* CRITICAL: Initialize stack FIRST, then enable sleep */
    esp_zb_init(&zb_nwk_cfg);
    
    /* Enable zigbee light sleep - MUST be called AFTER esp_zb_init() */
    esp_zb_sleep_enable(true);
    
    /* Configure device as Sleepy End Device (rx_on_when_idle = false) */
    esp_zb_set_rx_on_when_idle(false);
    
    /* Set sleep threshold - see configuration notes at top of file */
    esp_zb_sleep_set_threshold(ZIGBEE_SLEEP_THRESHOLD_MS);
    
    /* Validate timing configuration to prevent sleep conflicts */
    if (ZIGBEE_SLEEP_THRESHOLD_MS >= ZIGBEE_KEEP_ALIVE_MS) {
        ESP_LOGW(TAG, "⚠️ WARNING: Sleep threshold (%d ms) should be < keep_alive (%d ms) to avoid timing conflicts!", 
                 ZIGBEE_SLEEP_THRESHOLD_MS, ZIGBEE_KEEP_ALIVE_MS);
    }
    
    ESP_LOGI(TAG, "🔋 Configured as Sleepy End Device (SED) - rx_on_when_idle=false");
    ESP_LOGI(TAG, "📡 Keep-alive poll interval: %d ms (%.1f sec)", 
             ZIGBEE_KEEP_ALIVE_MS, ZIGBEE_KEEP_ALIVE_MS / 1000.0f);
    ESP_LOGI(TAG, "💤 Sleep threshold: %d ms (%.1f sec) - production optimized", 
             ZIGBEE_SLEEP_THRESHOLD_MS, ZIGBEE_SLEEP_THRESHOLD_MS / 1000.0f);
    ESP_LOGI(TAG, "⏱️  Parent timeout: 64 minutes");
    ESP_LOGI(TAG, "⚡ Power profile: 0.68mA sleep, 12mA transmit, ~0.83mA average");
    
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

    /* Endpoint 3 (Sleep Configuration) removed in light sleep mode.
     * Device uses automatic sleep/wake with standard Zigbee reporting configuration.
     * Reporting intervals controlled by coordinator via configureReporting commands.
     */

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
    
    /* In SED mode with automatic sleep, we don't manually schedule sleep operations.
     * The initial sensor report will be triggered after network join via the signal handler.
     * The device will automatically enter light sleep when idle (no Zigbee activity). */
    ESP_LOGI(TAG, "� SED automatic sleep mode configured - device will sleep when idle");
    
    /* Note: Button monitoring is now interrupt-based, no polling task needed */
    
    /* Start Zigbee stack main loop - required for Zigbee operation
     * Sleep happens automatically via ESP_ZB_COMMON_SIGNAL_CAN_SLEEP signal */
    ESP_LOGI(TAG, "⏳ Starting Zigbee stack main loop...");
    ESP_LOGI(TAG, "💤 Light sleep will be triggered automatically when stack is idle");
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
    
    /* Wake BME280 from sleep and trigger forced measurement */
    ret = bme280_app_wake_and_measure();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to wake BME280 for measurement: %s", esp_err_to_name(ret));
        /* Continue anyway - try to read cached values */
    }
    
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
    
    /* BME280 automatically returns to sleep mode after forced measurement.
     * No explicit sleep call needed - sensor is already in low-power state. */
    
    /* This function can be called by:
     * 1. Initial network join (one-time)
     * 2. Periodic timer (every 15 minutes)
     * 3. Coordinator's reporting configuration (based on min/max interval)
     * 
     * The periodic timer ensures regular updates even if coordinator doesn't
     * configure automatic reporting, while coordinator config can provide
     * additional event-driven reporting based on value changes.
     */
    ESP_LOGI(TAG, "📊 Sensor data reported. Device will enter light sleep until next event.");
}

/* Periodic sensor reading timer callback */
static void periodic_sensor_report_callback(void *arg)
{
    if (zigbee_network_connected) {
        ESP_LOGI(TAG, "⏰ Periodic sensor read timer fired (15-minute interval)");
        /* Schedule sensor reads via Zigbee scheduler to avoid ISR context issues.
         * Note: These functions update Zigbee attributes but don't force reporting.
         * The Zigbee stack will automatically send reports based on the coordinator's
         * reporting configuration (min/max intervals, reportable change thresholds). */
        esp_zb_scheduler_alarm((esp_zb_callback_t)bme280_read_and_report, 0, 100);
        esp_zb_scheduler_alarm((esp_zb_callback_t)rain_gauge_zigbee_update, 0, 200);
        /* Battery is read hourly based on its own time tracking */
        esp_zb_scheduler_alarm((esp_zb_callback_t)battery_read_and_report, 0, 300);
    } else {
        ESP_LOGW(TAG, "⏰ Periodic timer fired but network disconnected - skipping sensor read");
    }
}

/* Start periodic sensor reading timer */
static void start_periodic_reading(void)
{
    if (periodic_report_timer != NULL) {
        ESP_LOGW(TAG, "Periodic sensor reading timer already running");
        return;
    }
    
    const esp_timer_create_args_t periodic_timer_args = {
        .callback = &periodic_sensor_report_callback,
        .name = "periodic_read"
    };
    
    esp_err_t ret = esp_timer_create(&periodic_timer_args, &periodic_report_timer);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create periodic sensor reading timer: %s", esp_err_to_name(ret));
        return;
    }
    
    /* Start periodic timer - fires every 15 minutes to read sensors and update attributes.
     * Actual reporting to coordinator is controlled by Zigbee reporting configuration. */
    ret = esp_timer_start_periodic(periodic_report_timer, PERIODIC_READING_INTERVAL_MS * 1000ULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start periodic sensor reading timer: %s", esp_err_to_name(ret));
        esp_timer_delete(periodic_report_timer);
        periodic_report_timer = NULL;
        return;
    }
    
    ESP_LOGI(TAG, "⏰ Periodic sensor reading started: every %d minutes (%lu ms)", 
             SLEEP_DURATION_MINUTES, PERIODIC_READING_INTERVAL_MS);
    ESP_LOGI(TAG, "📡 Reporting to coordinator controlled by Zigbee reporting configuration");
}

/* Stop periodic sensor reading timer */
static void stop_periodic_reading(void)
{
    if (periodic_report_timer != NULL) {
        esp_timer_stop(periodic_report_timer);
        esp_timer_delete(periodic_report_timer);
        periodic_report_timer = NULL;
        ESP_LOGI(TAG, "⏰ Periodic sensor reading timer stopped");
    }
}

/* Rain gauge implementation */
static void IRAM_ATTR rain_gauge_isr_handler(void *arg)
{
    uint32_t gpio_num = RAIN_GAUGE_GPIO;
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;

    // ISR-safe: just increment a counter for debugging (can view in logs after interrupt)
    static uint32_t isr_trigger_count = 0;
    isr_trigger_count++;

    /* Prepare event with ISR tick for accurate timing */
    rain_evt_t evt = {
        .gpio_num = gpio_num,
        .tick = xTaskGetTickCountFromISR(),
    };
    xQueueSendFromISR(rain_gauge_evt_queue, &evt, &xHigherPriorityTaskWoken);
    
    if (xHigherPriorityTaskWoken) {
        portYIELD_FROM_ISR();
    }
}

static void rain_gauge_task(void *arg)
{
    rain_evt_t evt;
    TickType_t last_pulse_time = 0;
    const TickType_t DEBOUNCE_TIME = pdMS_TO_TICKS(200); // 200ms debounce
    
    ESP_LOGI(RAIN_TAG, "Rain gauge task started, waiting for events...");

    for (;;) {
        // Wait indefinitely for rain events - allows device to sleep when no rain
        if (xQueueReceive(rain_gauge_evt_queue, &evt, portMAX_DELAY)) {
            TickType_t current_time = evt.tick;
            
            // Simple debounce check
            if ((current_time - last_pulse_time) > DEBOUNCE_TIME) {
                // Verify GPIO is actually high (reed switch closed)
                if (gpio_get_level(evt.gpio_num) == 1) {
                    // Valid pulse - update counters
                    last_pulse_time = current_time;
                    rain_pulse_count++;
                    total_rainfall_mm += RAIN_MM_PER_PULSE;
                    total_rainfall_mm = roundf(total_rainfall_mm * 100.0f) / 100.0f; // Round to 2 decimals
                    
                    ESP_LOGI(RAIN_TAG, "🌧️ Rain pulse #%u: %.2f mm total (+%.2f mm)",
                             rain_pulse_count, total_rainfall_mm, RAIN_MM_PER_PULSE);
                    
                    // Save to NVS every 10 pulses
                    if (rain_pulse_count % 10 == 0) {
                        save_rainfall_data(total_rainfall_mm, rain_pulse_count);
                    }
                    
                    // Report to Zigbee if connected and threshold reached (1mm)
                    if (rain_gauge_enabled) {
                        float delta = total_rainfall_mm - last_reported_rainfall_mm;
                        if (delta >= REPORT_THRESHOLD_MM) {
                            ESP_LOGI(RAIN_TAG, "📡 Threshold reached (%.2f mm), reporting to Zigbee", delta);
                            esp_zb_scheduler_alarm((esp_zb_callback_t)rain_gauge_zigbee_update, 0, 10);
                        }
                    }
                } else {
                    ESP_LOGD(RAIN_TAG, "Pulse rejected - GPIO low (bounce)");
                }
            } else {
                ESP_LOGD(RAIN_TAG, "Pulse ignored - debounce active (%u ms)", 
                         pdTICKS_TO_MS(current_time - last_pulse_time));
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
        /* Ensure interrupt line is enabled so events will be delivered */
        gpio_intr_enable(RAIN_GAUGE_GPIO);
    } else if (ret == ESP_ERR_INVALID_STATE) {
        // ISR already installed - this is fine
        rain_gauge_isr_installed = true;
        ESP_LOGI(RAIN_TAG, "✅ Rain gauge ISR already installed, now enabled");
        /* Make sure the interrupt line is enabled in case it was disabled */
        gpio_intr_enable(RAIN_GAUGE_GPIO);
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
        /* Do not remove the ISR handler here; removing the handler causes a
         * gap where pulses cannot be queued. Instead, disable the GPIO
         * interrupt line so the handler remains installed and can capture
         * events when re-enabled. */
        gpio_intr_disable(RAIN_GAUGE_GPIO);
        rain_gauge_enabled = false;
        ESP_LOGI(RAIN_TAG, "Rain gauge interrupts disabled (handler kept)");
    } else {
        rain_gauge_enabled = false;
        ESP_LOGI(RAIN_TAG, "Rain gauge ISR already disabled");
    }
}

static void rain_gauge_zigbee_update(uint8_t param)
{
    // Round to 2 decimal places to avoid floating-point precision issues
    float rounded_rainfall = roundf(total_rainfall_mm * 100.0f) / 100.0f;
    
    esp_err_t ret = esp_zb_zcl_set_attribute_val(HA_ESP_RAIN_GAUGE_ENDPOINT, ESP_ZB_ZCL_CLUSTER_ID_ANALOG_INPUT,
                                                 ESP_ZB_ZCL_CLUSTER_SERVER_ROLE, ESP_ZB_ZCL_ATTR_ANALOG_INPUT_PRESENT_VALUE_ID,
                                                 &rounded_rainfall, true);  // Force reporting
    
    if (ret == ESP_OK) {
        ESP_LOGI(RAIN_TAG, "✅ Rain reported: %.2f mm", rounded_rainfall);
        last_reported_rainfall_mm = total_rainfall_mm;  // Update for next threshold check
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
// Hardware: R1=100kΩ, R2=100kΩ → theoretical divider = 2.0
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
                // Read last battery values from NVS and update Zigbee attributes
                float battery_voltage = 0.0f;
                float percentage = 0.0f;
                uint8_t zigbee_voltage = 0;
                uint8_t zigbee_percentage = 0;
                esp_err_t nvs_err = nvs_open("storage", NVS_READONLY, &nvs_handle);
                if (nvs_err == ESP_OK) {
                    nvs_get_u8(nvs_handle, "batt_zb_v", &zigbee_voltage);
                    nvs_get_u8(nvs_handle, "batt_zb_p", &zigbee_percentage);
                    nvs_get_blob(nvs_handle, "batt_v", &battery_voltage, &(size_t){sizeof(float)});
                    nvs_get_blob(nvs_handle, "batt_pct", &percentage, &(size_t){sizeof(float)});
                    nvs_close(nvs_handle);
                }
                // Update Zigbee attributes with last known values
                esp_zb_zcl_set_attribute_val(
                    HA_ESP_BME280_ENDPOINT,
                    ESP_ZB_ZCL_CLUSTER_ID_POWER_CONFIG,
                    ESP_ZB_ZCL_CLUSTER_SERVER_ROLE,
                    0x0020,
                    &zigbee_voltage,
                    false);
                esp_zb_zcl_set_attribute_val(
                    HA_ESP_BME280_ENDPOINT,
                    ESP_ZB_ZCL_CLUSTER_ID_POWER_CONFIG,
                    ESP_ZB_ZCL_CLUSTER_SERVER_ROLE,
                    0x0021,
                    &zigbee_percentage,
                    false);
                ESP_LOGI(BATTERY_TAG, "🔁 Restored battery values from NVS: %.2fV (%.0f%%) - Zigbee: %u, %u",
                         battery_voltage, percentage, zigbee_voltage, zigbee_percentage);
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
    // Store last measured values in NVS
    err = nvs_open("storage", NVS_READWRITE, &nvs_handle);
    if (err == ESP_OK) {
        nvs_set_u8(nvs_handle, "batt_zb_v", zigbee_voltage);
        nvs_set_u8(nvs_handle, "batt_zb_p", zigbee_percentage);
        nvs_set_blob(nvs_handle, "batt_v", &battery_voltage, sizeof(float));
        nvs_set_blob(nvs_handle, "batt_pct", &percentage, sizeof(float));
        nvs_commit(nvs_handle);
        nvs_close(nvs_handle);
    }
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

/* Rain gauge initialization and handlers */
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
    
    /* Check wake reason and count the wake-causing pulse if present. Record
     * the tick so we can avoid a duplicate count if an ISR event for the
     * same edge is queued immediately after. */
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
    
    /* Create queue for GPIO events (include ISR tick to disambiguate duplicates) */
    rain_gauge_evt_queue = xQueueCreate(10, sizeof(rain_evt_t));
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
    
    /* Install ISR handler now so pulses occurring after wake (but before
     * Zigbee network reconnect) are captured and queued. We still keep
     * `rain_gauge_enabled` false so reporting is deferred until connected.
     */
    esp_err_t add_ret = gpio_isr_handler_add(RAIN_GAUGE_GPIO, rain_gauge_isr_handler, NULL);
    if (add_ret == ESP_OK) {
        rain_gauge_isr_installed = true;
        ESP_LOGI(RAIN_TAG, "✅ ISR handler installed early on GPIO%d (will count pulses while offline)", RAIN_GAUGE_GPIO);
    } else if (add_ret == ESP_ERR_INVALID_STATE) {
        /* Already installed by some other initialization path - treat as installed */
        rain_gauge_isr_installed = true;
        ESP_LOGI(RAIN_TAG, "ℹ️ ISR handler already present for GPIO%d (counting will occur)", RAIN_GAUGE_GPIO);
    } else {
        ESP_LOGW(RAIN_TAG, "⚠️ Failed to add ISR handler early: %s - pulses while offline may be missed", esp_err_to_name(add_ret));
    }
    ESP_LOGI(RAIN_TAG, "Rain gauge GPIO configured; ISR installed for offline counting, reporting deferred until connected");
    
    /* Create rain gauge task */
    BaseType_t task_ret = xTaskCreate(rain_gauge_task, "rain_gauge_task", 2048, NULL, 5, NULL);
    if (task_ret != pdPASS) {
        ESP_LOGE(RAIN_TAG, "Failed to create rain gauge task");
        return;
    }
    
    /* Initialize reporting variables */
    last_reported_rainfall_mm = total_rainfall_mm;
    
    /* Add continuous GPIO monitoring for debugging */
    ESP_LOGI(RAIN_TAG, "Rain gauge initialized successfully. Current total: %.2f mm", total_rainfall_mm);
    ESP_LOGI(RAIN_TAG, "🔧 GPIO%d monitoring: level=%d, pull-down=enabled, trigger=RISING_EDGE", 
             RAIN_GAUGE_GPIO, gpio_get_level(RAIN_GAUGE_GPIO));
    
    /* Initial rain value will be reported after network join (see signal handler) */
    ESP_LOGI(RAIN_TAG, "Rain gauge ready - initial report will occur after network connection");
}

void app_main(void)
{
    /* Initialize NVS */
    ESP_ERROR_CHECK(nvs_flash_init());
    
    /* Initialize debug LED */
    debug_led_init();
    
    /* Initialize OTA */
    ESP_ERROR_CHECK(esp_zb_ota_init());
    
    /* Initialize power management for light sleep */
    ESP_ERROR_CHECK(esp_zb_power_save_init());
    
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
