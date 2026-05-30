/*
 * ESP Zigbee OTA Header
 * 
 * Handles Over-The-Air firmware updates via Zigbee network
 */

#pragma once

#include "esp_err.h"
#include "esp_zigbee_core.h"

// OTA manufacturer and image type definitions - now using CMakeLists.txt definitions
#ifndef OTA_MANUFACTURER
#define OTA_UPGRADE_MANUFACTURER  0xFABC    // Default DIY manufacturer code
#else
#define OTA_UPGRADE_MANUFACTURER  OTA_MANUFACTURER    // From CMakeLists.txt
#endif

#ifndef OTA_IMAGE_TYPE
#define OTA_UPGRADE_IMAGE_TYPE    0x1202    // Default image type (Caelum Pro - distinct from Caelum_Lite's 0x1200 so OTA images can't cross-flash)
#else
#define OTA_UPGRADE_IMAGE_TYPE    OTA_IMAGE_TYPE    // From CMakeLists.txt
#endif

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize OTA functionality
 * 
 * @return ESP_OK on success
 */
esp_err_t esp_zb_ota_init(void);

/**
 * @brief Get current OTA upgrade status
 * 
 * @return Current OTA status
 */
esp_zb_zcl_ota_upgrade_status_t esp_zb_ota_get_status(void);

/**
 * @brief Get current firmware version
 * 
 * @return Firmware version in format 0xMMmmpppp (major.minor.patch)
 */
uint32_t esp_zb_ota_get_fw_version(void);

/**
 * @brief Check if OTA update is in progress or pending
 * 
 * @return true if OTA is active, false otherwise
 */
bool esp_zb_ota_is_active(void);

/**
 * @brief OTA upgrade value callback handler
 * 
 * @param message OTA upgrade value message
 * @return ESP_OK on success
 */
esp_err_t zb_ota_upgrade_value_handler(esp_zb_zcl_ota_upgrade_value_message_t message);

/**
 * @brief OTA query image response callback handler
 * 
 * @param message OTA query image response message
 * @return ESP_OK on success
 */
esp_err_t zb_ota_query_image_resp_handler(esp_zb_zcl_ota_upgrade_query_image_resp_message_t message);

#ifdef __cplusplus
}
#endif
