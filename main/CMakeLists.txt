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
         "dps368.c"
         "as5600.c"
         "veml7700.c"
         "/home/fabian/esp/v5.5.1/esp-idf/examples/zigbee/common/zcl_utility/src/zcl_utility.c"
    INCLUDE_DIRS "." "/home/fabian/esp/v5.5.1/esp-idf/examples/zigbee/common/zcl_utility/include"
    PRIV_REQUIRES nvs_flash esp_driver_uart ieee802154 app_update esp_adc esp_timer
)

# Make generated build-time header visible to this component
target_include_directories(${COMPONENT_LIB} PRIVATE "${CMAKE_BINARY_DIR}/generated")

