# The following lines of boilerplate have to be in your project's CMakeLists
# in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
# "Trim" the build. Include the minimal set of components, main, and anything it depends on.
idf_build_set_property(MINIMAL_BUILD ON)


project(caelum)

# Version configuration
set(PROJECT_VER "1.8")
set(BUILD_NUMBER 0)
set(MANUFACTURER_CODE "0xFABC")
set(IMAGE_TYPE "0x1200")
set(ZIGBEE_STACK_VERSION "0x0003")  # Zigbee 3.0

# Convert PROJECT_VER (e.g. 1.1.0) to 0xMMmmpppp format for OTA
string(REPLACE "." ";" _ver_list ${PROJECT_VER})

list(GET _ver_list 0 _ver_major)
list(GET _ver_list 1 _ver_minor)
# Calculate OTA version dec
math(EXPR OTA_VERSION_DEC "((${_ver_major}) << 24) | ((${_ver_minor}) << 16) | (${BUILD_NUMBER})")
# Convert OTA_VERSION_DEC to 8-digit uppercase hex string (CMake <3.19 workaround)
set(_hex_chars "0123456789ABCDEF")
set(_value ${OTA_VERSION_DEC})
set(OTA_VERSION_HEX_STR "")
foreach(_i RANGE 1 8)
    math(EXPR _digit "${_value} % 16")
    string(SUBSTRING "${_hex_chars}" ${_digit} 1 _char)
    set(OTA_VERSION_HEX_STR "${_char}${OTA_VERSION_HEX_STR}")
    math(EXPR _value "${_value} / 16")
endforeach()

# Generate date code
string(TIMESTAMP DATE_CODE "%Y%m%d")

# Generate a small header with version macros so components can include a single
# header (works reliably across IDF/CMake versions and component layouts).
file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/generated")
configure_file(${CMAKE_SOURCE_DIR}/version.h.in ${CMAKE_BINARY_DIR}/generated/version.h @ONLY)

# (Top-level compile defs removed - version header is generated and used by components)
# Find Python
find_package(Python3 REQUIRED)


# Automatically generate Zigbee OTA image after .bin is created
add_custom_target(generate_ota ALL
    COMMAND ${CMAKE_COMMAND} -E echo "================================================"
    COMMAND ${CMAKE_COMMAND} -E echo "Generating Zigbee OTA image:"
    COMMAND ${CMAKE_COMMAND} -E echo "  Version: ${PROJECT_VER}.${BUILD_NUMBER} - 0x${OTA_VERSION_HEX_STR} - ${OTA_VERSION_DEC}"
    COMMAND ${CMAKE_COMMAND} -E echo "  Manufacturer: ${MANUFACTURER_CODE}"
    COMMAND ${CMAKE_COMMAND} -E echo "  Image Type: ${IMAGE_TYPE}"
    COMMAND ${CMAKE_COMMAND} -E echo "  Stack Version: ${ZIGBEE_STACK_VERSION}"
    COMMAND ${CMAKE_COMMAND} -E echo "================================================"
    COMMAND ${Python3_EXECUTABLE} 
            "$ENV{HOME}/Repositories/esp-zigbee-sdk/tools/image_builder_tool/image_builder_tool.py"
            -f "${CMAKE_BINARY_DIR}/${PROJECT_NAME}.bin"
            -c "${CMAKE_BINARY_DIR}/${PROJECT_NAME}_v${PROJECT_VER}.${BUILD_NUMBER}.ota"
            -m ${MANUFACTURER_CODE}
            -i ${IMAGE_TYPE}
            -v 0x${OTA_VERSION_HEX_STR}
            -s ${ZIGBEE_STACK_VERSION}
    COMMAND ${CMAKE_COMMAND} -E echo "OTA file generated: ${CMAKE_BINARY_DIR}/${PROJECT_NAME}_v${PROJECT_VER}.${BUILD_NUMBER}.ota"
    DEPENDS ${PROJECT_NAME}.elf
    BYPRODUCTS "${CMAKE_BINARY_DIR}/${PROJECT_NAME}_v${PROJECT_VER}_${BUILD_NUMBER}.ota"
    COMMENT "Generating Zigbee OTA image v${PROJECT_VER} (0x${OTA_VERSION_HEX_STR})"
)

