[![Support me on Ko-fi](https://ko-fi.com/img/githubbutton_sm.svg)](https://ko-fi.com/Fabiancrg)

| Supported Targets | ESP32-C6 | ESP32-H2 |
| ----------------- |  -------- | -------- |

# Zigbee Weather Station

[![License: GPL v3](https://img.shields.io/badge/Software-GPLv3-blue.svg)](./LICENSE)
[![License: CC BY-NC-SA 4.0](https://img.shields.io/badge/Hardware-CC%20BY--NC--SA%204.0-green.svg)](./LICENSE-hardware)

## Project Description

This project implements a battery-powered environmental monitoring device using ESP32-C6/H2 with Zigbee connectivity. The device features a simplified 2-endpoint design focused on essential weather station functionality, optimized for low-power operation with deep sleep support.

This project is based on the examples provided in the ESP Zigbee SDK:

* [ESP Zigbee SDK Docs](https://docs.espressif.com/projects/esp-zigbee-sdk)
* [ESP Zigbee SDK Repo](https://github.com/espressif/esp-zigbee-sdk)

## Device Features

### 🌐 Zigbee Endpoints Overview

| Endpoint | Device Type | Clusters | Description |
|----------|-------------|----------|-------------|
| **1** | Environmental Sensor | Temperature, Humidity, Pressure | BME280 sensor via I2C |
| **2** | Rain Gauge | Analog Input | Tipping bucket rain sensor with rainfall totals |

### 📋 Detailed Endpoint Descriptions

#### **Endpoint 1: Environmental Monitoring**
- **Hardware**: BME280 sensor via I2C
  - **ESP32-H2**: SDA: GPIO1, SCL: GPIO2
  - **ESP32-C6**: SDA: GPIO6, SCL: GPIO7
- **Measurements**: 
  - 🌡️ **Temperature**: -40°C to +85°C (±1°C accuracy)
  - 💧 **Humidity**: 0-100% RH (±3% accuracy) 
  - 🌪️ **Pressure**: 300-1100 hPa (±1 hPa accuracy)
- **Features**: Automatic 30-second reporting, Zigbee-standard units
- **Use Case**: Weather monitoring, HVAC automation, air quality tracking

#### **Endpoint 2: Rain Gauge System**
- **Hardware**: Tipping bucket rain gauge with reed switch
  - **ESP32-H2**: GPIO12 (RTC-capable for sleep wake-up)
  - **ESP32-C6**: GPIO18 (timer wake-up only)
- **Measurements**: Cumulative rainfall in millimeters (0.36mm per tip)
- **Features**: 
  - Advanced debouncing (200ms + 1000ms bounce settle)
  - Persistent storage (NVS) for total tracking
  - Smart reporting (1mm threshold OR hourly)
  - Network-aware operation (only active when connected)
  - ESP32-H2: Wake from deep sleep on rain detection
- **Specifications**: 
  - Maximum rate: 200mm/hour supported
  - Accuracy: ±0.36mm per bucket tip
  - Storage: Non-volatile total persistence across reboots
- **Use Case**: Weather station, irrigation control, flood monitoring

### 🔧 Hardware Configuration

#### **Required Components**
- ESP32-C6 or ESP32-H2 development board
- BME280 environmental sensor module
- Tipping bucket rain gauge with reed switch
- Zigbee coordinator (ESP32-H2 or commercial gateway)

#### **Pin Assignments**

**ESP32-H2 (Recommended)**
```
GPIO 1  - I2C SDA (BME280)
GPIO 2  - I2C SCL (BME280) 
GPIO 9  - Built-in button (factory reset)
GPIO 12 - Rain gauge input (RTC-capable)*
```

**ESP32-C6**
```
GPIO 6  - I2C SDA (BME280)
GPIO 7  - I2C SCL (BME280) 
GPIO 9  - Built-in button (factory reset)
GPIO 18 - Rain gauge input (timer wake-up only)**
```

*ESP32-H2: GPIO12 is RTC-capable, enabling rain detection during deep sleep  
**ESP32-C6: GPIO18 is not RTC-capable, rain detection limited to timer wake-ups

### � Zigbee Integration
- **Protocol**: Zigbee 3.0  
- **Device Type**: End Device (sleepy end device for battery operation)
- **Supported Channels**: 11-26 (2.4 GHz)
- **Compatible**: Zigbee2MQTT, Home Assistant ZHA, Hubitat
- **OTA Support**: Over-the-air firmware updates enabled (see [OTA_GUIDE.md](OTA_GUIDE.md))

### ⚡ Power Management
- **Deep Sleep Mode**: 15-minute wake intervals for battery operation
- **Wake-up Sources**:
  - Timer (15 minutes for periodic updates)
  - **ESP32-H2**: Rain detection during deep sleep (GPIO12 RTC-capable)
  - **ESP32-C6**: Rain detection limited to timer wake-ups (GPIO18 not RTC-capable)
- **Battery Life**: Optimized for extended operation on battery power
- **Power Consumption**: ~100mA active, 7-10µA in deep sleep
- **Battery Estimate**: 3+ years with 2500mAh battery on ESP32-H2

## 🚀 Quick Start

### Prerequisites

```bash
# Install ESP-IDF v5.5.1 or later
git clone -b v5.5.1 --recursive https://github.com/espressif/esp-idf.git
cd esp-idf
# For ESP32-H2 (recommended)
./install.sh esp32h2
# For ESP32-C6
./install.sh esp32c6
. ./export.sh
```

### Configure the Project
```bash
# For ESP32-H2 (recommended for better power management)
idf.py set-target esp32h2
# For ESP32-C6  
idf.py set-target esp32c6
idf.py menuconfig
```

### Build and Flash
```bash
# Erase previous data (recommended for first flash)
idf.py -p [PORT] erase-flash

# Build and flash the project
idf.py -p [PORT] flash monitor
```

### 🛠️ Configuration

### Device Operation

#### **Factory Reset**
- **Built-in Button (GPIO 9)**:
  - Long press (5s): Factory reset device

#### **Automatic Features**
- Environmental data reported every 30 seconds (when awake)
- Rain gauge totals stored persistently in NVS
- Smart rainfall reporting (1mm increments or hourly)
- Network connection status monitoring
- Deep sleep after 10 seconds of operation

### 📡 Data Reporting
- **Temperature/Humidity/Pressure**: Reported during wake cycles
- **Rainfall**: Immediate on tip detection (>1mm) or hourly
- **Sleep Cycle**: 15-minute intervals for battery conservation

## 📊 Example Output

### Device Initialization
```
I (403) app_start: Starting scheduler on CPU0
I (408) WEATHER_STATION: Initialize Zigbee stack
I (558) WEATHER_STATION: Deferred driver initialization successful
I (568) WEATHER_STATION: BME280 sensor initialized successfully
I (578) WEATHER_STATION: Rain gauge initialized. Current total: 0.00 mm
I (578) WEATHER_STATION: Start network steering
```

### Network Connection
```
I (3558) WEATHER_STATION: Joined network successfully (Extended PAN ID: 74:4d:bd:ff:fe:63:f7:30, PAN ID: 0x13af, Channel:13, Short Address: 0x7c16)
I (3568) RAIN_GAUGE: Rain gauge enabled - device connected to Zigbee network
```

### Sensor Data Reporting
```
I (8000) WEATHER_STATION: 🌡️ Temperature: 22.35°C reported to Zigbee  
I (8010) WEATHER_STATION: 💧 Humidity: 45.20% reported to Zigbee
I (8020) WEATHER_STATION: 🌪️ Pressure: 1013.25 hPa reported to Zigbee
I (8030) WEATHER_STATION: 📡 Temp: 22.4°C
I (8040) WEATHER_STATION: 📡 Humidity: 45.2%
I (8050) WEATHER_STATION: 📡 Pressure: 1013.3 hPa
```

### Rain Gauge Activity
```  
I (10000) RAIN_GAUGE: 🔍 Rain gauge interrupt received on GPIO18 (enabled: YES)
I (10010) RAIN_GAUGE: 🌧️ Rain pulse #1 detected! Total: 0.36 mm (+0.36 mm)
I (10020) RAIN_GAUGE: ✅ Rainfall total 0.36 mm reported to Zigbee
I (10030) WEATHER_STATION: 📡 Rain: 0.36 mm
```

### Deep Sleep Entry
```
I (18000) WEATHER_STATION: ⏳ Preparing for deep sleep...
I (18010) WEATHER_STATION: 💤 Entering deep sleep for 900 seconds (15 minutes)
```

## 🏠 Home Assistant Integration

When connected to Zigbee2MQTT or other Zigbee coordinators, the device appears as:

- **3x Sensor entities**: Temperature, Humidity, Pressure  
- **1x Sensor entity**: Rainfall total with automatic updates

### Device Information
- **Manufacturer**: ESPRESSIF
- **Model**: WeatherStation  
- **Firmware**: v1.0.0
- **Supported**: Automatic device detection in Zigbee2MQTT

## 🔧 Configuration

Key parameters can be adjusted in `main/esp_zb_weather.h`:
- Sleep duration (default: 15 minutes)
- Reporting intervals
- Rain tip bucket volume (default: 0.36mm)
- Wake-up thresholds

## 📝 Project Structure

```
WeatherStation/
├── main/
│   ├── esp_zb_weather.c     # Main Zigbee stack and sensor logic
│   ├── esp_zb_weather.h     # Configuration and headers
│   ├── esp_zb_ota.c         # OTA update implementation
│   ├── esp_zb_ota.h         # OTA interface
│   ├── sleep_manager.c      # Deep sleep management with RTC GPIO support
│   ├── sleep_manager.h      # Sleep manager interface
│   ├── bme280_app.c         # BME280 sensor driver with board-specific I2C
│   ├── bme280_app.h         # BME280 interface
│   ├── weather_driver.c     # DEPRECATED: Legacy driver (unused)
│   └── weather_driver.h     # DEPRECATED: Legacy interface (unused)
├── CMakeLists.txt           # Build configuration with app_update dependency
├── partitions.csv           # Partition table with OTA support
├── sdkconfig.defaults       # Default SDK settings
├── OTA_GUIDE.md            # OTA update instructions
├── DEEP_SLEEP_IMPLEMENTATION.md # Deep sleep implementation details
└── README.md               # This file
```

## 🔧 Troubleshooting

### Common Issues

#### **Rain Gauge Not Detecting**
- **ESP32-H2**: Verify GPIO12 connections and reed switch operation
- **ESP32-C6**: Verify GPIO18 connections (rain detection during timer wake-ups only)
- Check that device is connected to Zigbee network (rain gauge only active when connected)
- Ensure proper pull-down resistor on rain gauge input

#### **BME280 Not Reading**  
- **ESP32-H2**: Check I2C connections (SDA: GPIO1, SCL: GPIO2)
- **ESP32-C6**: Check I2C connections (SDA: GPIO6, SCL: GPIO7)
- Verify BME280 I2C address (default: 0x76 or 0x77)
- Ensure proper power supply to sensor (3.3V)

#### **Zigbee Connection Issues**
- Perform factory reset with long press (5s) on built-in button
- Ensure Zigbee coordinator is in pairing mode
- Check channel compatibility between coordinator and device

#### **Device Not Waking from Sleep**
- Check battery voltage (minimum 3.0V recommended)
- Verify wake-up sources (timer, rain gauge)
- Review sleep manager logs for errors

### 📋 Development Notes

- **ESP-IDF Version**: v5.5.1 recommended
- **Zigbee SDK**: Latest ESP Zigbee SDK required  
- **Memory Usage**: ~2MB flash, ~200KB RAM typical
- **Power Consumption**: ~100mA active, 7-10µA in deep sleep
- **Battery Operation**: Optimized for CR123A or Li-ion battery packs
- **Target Recommendation**: ESP32-H2 preferred for RTC GPIO wake-up capabilities

## 🔄 OTA Updates

This project supports Over-The-Air (OTA) firmware updates via Zigbee network. See [OTA_GUIDE.md](OTA_GUIDE.md) for detailed instructions on:
- Creating OTA images
- Configuring Zigbee2MQTT for OTA
- Performing updates
- Troubleshooting OTA issues

## 📄 License

This project follows dual licensing:
- **Software**: GNU General Public License v3.0 (see [LICENSE](LICENSE))
- **Hardware**: Creative Commons Attribution-NonCommercial-ShareAlike 4.0 (see [LICENSE-hardware](LICENSE-hardware))

## 🔄 Based On

This project is derived from ESP32 Zigbee SDK examples and inherits the multi-endpoint architecture optimized for battery-powered weather station applications with deep sleep support.

## 🤝 Contributing

Contributions are welcome! Please feel free to submit issues or pull requests.

For technical queries, please open an issue on GitHub. Include:
- Complete serial monitor output
- Hardware configuration details  
- ESP-IDF and SDK versions
- Specific symptoms and reproduction steps

## 💖 Support

If you find this project useful, consider supporting the development:

[![ko-fi](https://ko-fi.com/img/githubbutton_sm.svg)](https://ko-fi.com/Fabiancrg)

---

**Project**: ESP32 Zigbee Weather Station  
**Version**: v1.0.0  
**Compatible**: ESP32-C6, ESP32-H2, ESP-IDF v5.5.1+  
**License**: GPL v3 (Software) / CC BY-NC-SA 4.0 (Hardware)
