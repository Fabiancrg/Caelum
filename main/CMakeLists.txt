set(ZCL_UTILITY_OLD_BASE "${IDF_PATH}/examples/zigbee/common/zcl_utility")
set(ZCL_UTILITY_NEW_BASE "${IDF_PATH}/examples/zigbee/zb_common_components/examples_utils")

idf_component_register(
    SRCS "esp_zb_weather.c"
         "esp_zb_ota.c"
         "weather_driver.c"
         "bme280_app.c"
         "sensor_if.c"
         "sht41.c"
         "aht20.c"
         "bmp280.c"
         "sleep_manager.c"
         "i2c_config.c"
         "battery_monitor.c"
         "anemometer.c"
         "lps22hb.c"
         "dps368.c"
         "as5600.c"
         "veml7700.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES nvs_flash esp_driver_uart ieee802154 app_update esp_adc esp_timer
)

if(EXISTS "${ZCL_UTILITY_OLD_BASE}/src" AND EXISTS "${ZCL_UTILITY_OLD_BASE}/include")
    target_sources(${COMPONENT_LIB} PRIVATE "${ZCL_UTILITY_OLD_BASE}/src/zcl_utility.c")
    target_include_directories(${COMPONENT_LIB} PRIVATE "${ZCL_UTILITY_OLD_BASE}/include")
elseif(EXISTS "${ZCL_UTILITY_NEW_BASE}/zcl_utility.c" AND EXISTS "${ZCL_UTILITY_NEW_BASE}/include")
    target_sources(${COMPONENT_LIB} PRIVATE "${ZCL_UTILITY_NEW_BASE}/zcl_utility.c")
    target_include_directories(${COMPONENT_LIB} PRIVATE "${ZCL_UTILITY_NEW_BASE}/include")
else()
    message(FATAL_ERROR "Unable to locate Zigbee zcl_utility in ESP-IDF. Checked ${ZCL_UTILITY_OLD_BASE} and ${ZCL_UTILITY_NEW_BASE}")
endif()

# Make generated build-time header visible to this component
target_include_directories(${COMPONENT_LIB} PRIVATE "${CMAKE_BINARY_DIR}/generated")

