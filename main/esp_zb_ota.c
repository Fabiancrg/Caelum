/*
 * ESP Zigbee OTA Implementation
 * 
 * Handles Over-The-Air firmware updates via Zigbee network
 */

#include "esp_zb_ota.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_app_format.h"
#include "nvs_flash.h"
#include "esp_zigbee_core.h"
#include "zcl/esp_zigbee_zcl_ota.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "esp_sleep.h"
#include "esp_pm.h"

static const char *TAG = "ESP_ZB_OTA";

/* OTA upgrade status */
static esp_zb_zcl_ota_upgrade_status_t ota_upgrade_status = ESP_ZB_ZCL_OTA_UPGRADE_STATUS_OK;

/* OTA in progress flag - used to prevent sleep during transfer */
static bool ota_transfer_active = false;

/* Power Management lock to prevent CPU frequency scaling and sleep during OTA */
static esp_pm_lock_handle_t ota_pm_lock = NULL;

/* OTA partition handle */
static const esp_partition_t *update_partition = NULL;
static esp_ota_handle_t update_handle = 0;
static uint32_t total_received = 0;
static uint32_t total_image_size = 0;

/**
 * @brief Initialize OTA functionality
 */
esp_err_t esp_zb_ota_init(void)
{
    ESP_LOGI(TAG, "Initializing Zigbee OTA");

    /* Create Power Management lock to prevent sleep during OTA */
    esp_err_t ret = esp_pm_lock_create(ESP_PM_NO_LIGHT_SLEEP, 0, "ota_no_sleep", &ota_pm_lock);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to create PM lock: %s (OTA will still work but may have issues)",
                 esp_err_to_name(ret));
        ota_pm_lock = NULL;
    }

    // Get the currently running partition
    const esp_partition_t *running_partition = esp_ota_get_running_partition();
    if (running_partition != NULL) {
        ESP_LOGI(TAG, "Currently running from partition: %s at 0x%lx", 
                 running_partition->label, 
                 running_partition->address);
    }
    
    // Get the next OTA partition
    update_partition = esp_ota_get_next_update_partition(NULL);
    if (update_partition == NULL) {
        ESP_LOGE(TAG, "Failed to find OTA partition");
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "OTA partition found: %s at 0x%lx (size: %ld bytes)", 
             update_partition->label, 
             update_partition->address, 
             update_partition->size);
    
    // Check if we're in OTA validation mode (first boot after OTA update)
    esp_ota_img_states_t ota_state;
    if (esp_ota_get_state_partition(running_partition, &ota_state) == ESP_OK) {
        if (ota_state == ESP_OTA_IMG_PENDING_VERIFY) {
            ESP_LOGW(TAG, "*** OTA UPDATE VALIDATION MODE ***");
            ESP_LOGW(TAG, "New firmware is running but not yet validated");
            ESP_LOGW(TAG, "If system is stable, firmware will be marked valid");
            
            // Mark the new firmware as valid if we reached here successfully
            // This prevents rollback to the previous version
            esp_err_t ret = esp_ota_mark_app_valid_cancel_rollback();
            if (ret == ESP_OK) {
                ESP_LOGI(TAG, "✓ OTA update validated successfully!");
                ESP_LOGI(TAG, "New firmware is now permanent");
            } else {
                ESP_LOGE(TAG, "Failed to validate OTA update: %s", esp_err_to_name(ret));
            }
        } else {
            ESP_LOGI(TAG, "Running stable firmware (not in OTA validation mode)");
        }
    }
    
    return ESP_OK;
}

/**
 * @brief OTA upgrade callback handler for client device
 */
esp_err_t zb_ota_upgrade_value_handler(esp_zb_zcl_ota_upgrade_value_message_t message)
{
    esp_err_t ret = ESP_OK;

    switch (message.upgrade_status) {
        case ESP_ZB_ZCL_OTA_UPGRADE_STATUS_START:
            ESP_LOGI(TAG, "=== OTA UPGRADE STARTED ===");
            ota_upgrade_status = ESP_ZB_ZCL_OTA_UPGRADE_STATUS_START;
            ota_transfer_active = true;
            total_received = 0;
            total_image_size = message.ota_header.total_image_size;

            /* CRITICAL: For Sleepy End Devices, prevent ALL sleep modes */
            esp_zb_sleep_enable(false);
            esp_zb_set_rx_on_when_idle(true);

            if (ota_pm_lock != NULL) {
                ret = esp_pm_lock_acquire(ota_pm_lock);
                if (ret != ESP_OK) {
                    ESP_LOGW(TAG, "Failed to acquire PM lock: %s", esp_err_to_name(ret));
                }
            }

            // Begin OTA update
            ret = esp_ota_begin(update_partition, OTA_SIZE_UNKNOWN, &update_handle);
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "esp_ota_begin failed: %s", esp_err_to_name(ret));
                ota_upgrade_status = ESP_ZB_ZCL_OTA_UPGRADE_STATUS_ERROR;
                if (ota_pm_lock != NULL) esp_pm_lock_release(ota_pm_lock);
                esp_zb_sleep_enable(true);
                esp_zb_set_rx_on_when_idle(false);
                ota_transfer_active = false;
                return ret;
            }
            ESP_LOGI(TAG, "OTA write session started (sleep disabled, RX always-on)");
            break;

        case ESP_ZB_ZCL_OTA_UPGRADE_STATUS_RECEIVE:
            // Handle the first chunk specially to detect and skip OTA header
            if (total_received == 0) {
                ESP_LOGI(TAG, "First chunk received: %d bytes", message.payload_size);
                ESP_LOG_BUFFER_HEX_LEVEL(TAG, message.payload, 
                                        message.payload_size > 128 ? 128 : message.payload_size, 
                                        ESP_LOG_DEBUG);
                
                // Search for the ESP32 magic byte (0xE9) in the first chunk
                int magic_offset = -1;
                for (int i = 0; i < message.payload_size && i < 256; i++) {
                    if (message.payload[i] == 0xE9) {
                        magic_offset = i;
                        ESP_LOGI(TAG, "Found ESP32 magic byte (0xE9) at offset %d", magic_offset);
                        break;
                    }
                }
                
                if (magic_offset >= 0) {
                    // Found the ESP32 binary, skip everything before it
                    ESP_LOGI(TAG, "Skipping %d bytes before ESP32 binary", magic_offset);
                    ESP_LOGI(TAG, "Writing %d bytes from first chunk", 
                             message.payload_size - magic_offset);
                    
                    ret = esp_ota_write(update_handle, message.payload + magic_offset, 
                                      message.payload_size - magic_offset);
                    total_received += message.payload_size - magic_offset;
                } else {
                    ESP_LOGE(TAG, "No ESP32 magic byte (0xE9) found in first %d bytes", 
                             message.payload_size);
                    ESP_LOGE(TAG, "Cannot proceed with OTA update");
                    ota_upgrade_status = ESP_ZB_ZCL_OTA_UPGRADE_STATUS_ERROR;
                    return ESP_ERR_INVALID_ARG;
                }
            } else {
                // Subsequent chunks - write directly
                ESP_LOGD(TAG, "OTA receiving chunk: %d bytes (total: %ld bytes)",
                         message.payload_size, total_received);
                
                ret = esp_ota_write(update_handle, message.payload, message.payload_size);
                total_received += message.payload_size;
            }
            
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "esp_ota_write failed: %s", esp_err_to_name(ret));
                ota_upgrade_status = ESP_ZB_ZCL_OTA_UPGRADE_STATUS_ERROR;
                if (ota_pm_lock != NULL) esp_pm_lock_release(ota_pm_lock);
                esp_zb_sleep_enable(true);
                esp_zb_set_rx_on_when_idle(false);
                ota_transfer_active = false;
                return ret;
            }

            // Log progress every ~100KB
            static uint32_t last_milestone = 0;
            uint32_t current_milestone = total_received / 100000;
            if (current_milestone > last_milestone) {
                ESP_LOGI(TAG, "OTA progress: %ld KB (%.1f%%)", total_received / 1000,
                         total_image_size > 0 ? (float)total_received / (float)total_image_size * 100.0f : 0.0f);
                last_milestone = current_milestone;
            }
            break;

        case ESP_ZB_ZCL_OTA_UPGRADE_STATUS_APPLY:
            ESP_LOGI(TAG, "=== OTA UPGRADE APPLY ===");

            // Finish OTA write
            ret = esp_ota_end(update_handle);
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "esp_ota_end failed: %s", esp_err_to_name(ret));
                ota_upgrade_status = ESP_ZB_ZCL_OTA_UPGRADE_STATUS_ERROR;
                if (ota_pm_lock != NULL) esp_pm_lock_release(ota_pm_lock);
                esp_zb_sleep_enable(true);
                esp_zb_set_rx_on_when_idle(false);
                ota_transfer_active = false;
                return ret;
            }

            // Verify the written image
            ESP_LOGI(TAG, "Verifying OTA image...");
            esp_app_desc_t new_app_info;
            ret = esp_ota_get_partition_description(update_partition, &new_app_info);
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "Failed to get new app description: %s", esp_err_to_name(ret));
                ota_upgrade_status = ESP_ZB_ZCL_OTA_UPGRADE_STATUS_ERROR;
                return ret;
            }
            
            ESP_LOGI(TAG, "New firmware version: %s", new_app_info.version);
            ESP_LOGI(TAG, "New firmware compile time: %s %s", new_app_info.date, new_app_info.time);

            // CRITICAL: Set boot partition to the newly written partition
            ret = esp_ota_set_boot_partition(update_partition);
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "esp_ota_set_boot_partition failed: %s", esp_err_to_name(ret));
                ota_upgrade_status = ESP_ZB_ZCL_OTA_UPGRADE_STATUS_ERROR;
                return ret;
            }

            ESP_LOGI(TAG, "✓ OTA upgrade successful!");
            ESP_LOGI(TAG, "Total received: %ld bytes", total_received);
            ESP_LOGI(TAG, "Next boot will be from: %s", update_partition->label);

            /* Re-enable sleep before reboot */
            ota_transfer_active = false;
            esp_zb_sleep_enable(true);
            esp_zb_set_rx_on_when_idle(false);
            if (ota_pm_lock != NULL) esp_pm_lock_release(ota_pm_lock);

            ESP_LOGI(TAG, "Rebooting in 3 seconds...");

            ota_upgrade_status = ESP_ZB_ZCL_OTA_UPGRADE_STATUS_APPLY;
            
            // Small delay to ensure logs are printed
            vTaskDelay(pdMS_TO_TICKS(3000));
            
            // Reboot to apply the update
            // After reboot, the new firmware will be in ESP_OTA_IMG_PENDING_VERIFY state
            // and must call esp_ota_mark_app_valid_cancel_rollback() to confirm it works
            esp_restart();
            break;

        case ESP_ZB_ZCL_OTA_UPGRADE_STATUS_CHECK:
            ESP_LOGI(TAG, "OTA upgrade check - manufacturer: 0x%04X, image_type: 0x%04X, file_version: 0x%08lX, size: %ld",
                     message.ota_header.manufacturer_code, message.ota_header.image_type,
                     (unsigned long)message.ota_header.file_version, (long)message.ota_header.total_image_size);
            ret = ESP_OK;
            break;

        case ESP_ZB_ZCL_OTA_UPGRADE_STATUS_FINISH:
            ESP_LOGI(TAG, "✓ OTA upgrade finished successfully");
            ota_upgrade_status = ESP_ZB_ZCL_OTA_UPGRADE_STATUS_FINISH;
            break;

        case ESP_ZB_ZCL_OTA_UPGRADE_STATUS_ERROR:
            ESP_LOGE(TAG, "✗ OTA upgrade error");
            ota_upgrade_status = ESP_ZB_ZCL_OTA_UPGRADE_STATUS_ERROR;
            ota_transfer_active = false;

            /* Re-enable sleep after OTA failure */
            esp_zb_sleep_enable(true);
            esp_zb_set_rx_on_when_idle(false);
            if (ota_pm_lock != NULL) esp_pm_lock_release(ota_pm_lock);

            // Abort OTA if it was started
            if (update_handle) {
                esp_ota_abort(update_handle);
                update_handle = 0;
            }
            ret = ESP_FAIL;
            break;

        default:
            ESP_LOGW(TAG, "Unknown OTA status: %d", message.upgrade_status);
            break;
    }

    return ret;
}

/**
 * @brief OTA query image response handler
 */
esp_err_t zb_ota_query_image_resp_handler(esp_zb_zcl_ota_upgrade_query_image_resp_message_t message)
{
    esp_err_t ret = ESP_OK;

    if (message.info.status == ESP_ZB_ZCL_STATUS_SUCCESS) {
        ESP_LOGI(TAG, "📦 OTA image available: version 0x%lx, size %ld bytes",
                 message.file_version, message.image_size);
        ret = ESP_OK;
    } else {
        ESP_LOGD(TAG, "No OTA image available or query failed");
        ret = ESP_FAIL;
    }

    return ret;
}

/**
 * @brief Get current OTA status
 */
esp_zb_zcl_ota_upgrade_status_t esp_zb_ota_get_status(void)
{
    return ota_upgrade_status;
}

/**
 * @brief Get current firmware version
 */
uint32_t esp_zb_ota_get_fw_version(void)
{
#ifdef OTA_FILE_VERSION
    // Use version from CMake (already in 0xMMmmpppp format)
    ESP_LOGI(TAG, "Firmware version from CMake: " FW_VERSION " (0x%08lX)", (unsigned long)OTA_FILE_VERSION);
    return OTA_FILE_VERSION;
#else
    // Fallback: parse from app description
    const esp_app_desc_t *app_desc = esp_app_get_description();
    uint32_t version = 0;
    if (app_desc) {
        int major = 0, minor = 0, patch = 0;
        // Only parse if string is in semver format, else fallback to 0x01000000
        if (sscanf(app_desc->version, "%d.%d.%d", &major, &minor, &patch) == 3) {
            version = ((major & 0xFF) << 24) | ((minor & 0xFF) << 16) | (patch & 0xFFFF);
            ESP_LOGI(TAG, "Firmware version: %s (0x%08lX)", app_desc->version, version);
        } else {
            // Fallback: use 1.0.0 if not a semver string
            version = 0x01000000;
            ESP_LOGW(TAG, "Firmware version string not semver: '%s', using fallback 1.0.0 (0x01000000)", app_desc->version);
        }
    }
    return version;
#endif
}

/**
 * @brief Check if OTA update is in progress or pending
 */
bool esp_zb_ota_is_active(void)
{
    return (ota_upgrade_status == ESP_ZB_ZCL_OTA_UPGRADE_STATUS_START ||
            ota_upgrade_status == ESP_ZB_ZCL_OTA_UPGRADE_STATUS_RECEIVE ||
            ota_upgrade_status == ESP_ZB_ZCL_OTA_UPGRADE_STATUS_APPLY);
}
