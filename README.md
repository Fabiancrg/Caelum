[![Support me on Ko-fi](https://ko-fi.com/img/githubbutton_sm.svg)](https://ko-fi.com/Fabiancrg)

| Supported Target | ESP32-H2 |
| ---------------- | -------- |

# Caelum - Zigbee Weather Station

[![License: GPL v3](https://img.shields.io/badge/Software-GPLv3-blue.svg)](./LICENSE)
[![License: CC BY-NC-SA 4.0](https://img.shields.io/badge/Hardware-CC%20BY--NC--SA%204.0-green.svg)](./LICENSE-hardware)

## Project Description

This project implements a battery-powered comprehensive weather station using ESP32-H2 with Zigbee connectivity. The device operates as a Zigbee Sleepy End Device (SED) with automatic light sleep for ultra-low power consumption while maintaining network responsiveness.

**Hardware v2.0** features dual I2C buses supporting multiple sensor combinations, dedicated rain gauge and anemometer inputs, DS18B20 temperature sensor, light sensor, and wind direction sensor for professional weather monitoring capabilities.

## Device Features

### 🌐 Zigbee Endpoints Overview (v2.0 Hardware)

| Endpoint | Device Type | Clusters | Description |
|----------|-------------|----------|-------------|
| **1** | Environmental Sensor | Temperature, Humidity, Pressure, Battery, OTA | Multi-sensor support via dual I2C buses |
| **2** | Rain Gauge | Analog Input | Tipping bucket rain sensor with rainfall totals (GPIO12) |
| **3** | DS18B20 Temperature | Temperature Measurement | External waterproof temperature probe (GPIO24) |
| **4** | Anemometer (Wind Speed) | Analog Input | Wind speed sensor with pulse counting (GPIO14) |
| **5** | Wind Direction | Analog Input | AS5600 magnetic wind vane (I2C Bus 2) |
| **6** | Light Sensor | Illuminance Measurement | VEML7700 ambient light sensor (I2C Bus 2) |

### 💡 LED Boot Indicator (Always Active)

The device includes a WS2812 RGB LED on GPIO8 (ESP32-H2) that provides visual feedback during boot and network join:
- **Yellow (Blinking)**: Joining/searching for network  
- **Blue (Steady, 5 seconds)**: Successfully connected to Zigbee network
- **Red (Blinking, 10 times)**: Connection failed after max retries
- **Off**: LED powered down after boot sequence (RMT peripheral disabled for power savings)

**Power Optimization**: The LED and its RMT peripheral are permanently disabled after the initial boot/join sequence to save ~1-2mA. This is critical for achieving 0.68mA sleep current. The LED cannot be re-enabled remotely - it only operates during the first boot cycle.

### 📋 Detailed Endpoint Descriptions (v2.0)

#### **Endpoint 1: Environmental Monitoring & Power Management**
- **I2C Bus 1 (GPIO10/11)**: Temperature, Humidity, Pressure sensors
- **Supported Sensors** (automatically detected):
  - **SHT41** (Bus 1): High-accuracy Temperature + Humidity (±0.2°C, ±1.8% RH)
  - **AHT20** (Bus 1): Alternative Temperature + Humidity sensor
  - **BMP280** (Bus 1): Temperature + Pressure (±1 hPa accuracy)
  - **BME280** (Bus 1): All-in-one Temperature + Humidity + Pressure
  - **DPS368** (Bus 1): High-precision Pressure sensor (±0.002 hPa)
- **Measurements**: 
  - 🌡️ **Temperature**: -40°C to +85°C (multiple sources available)
  - 💧 **Humidity**: 0-100% RH (from SHT41/AHT20/BME280)
  - 🌪️ **Pressure**: 300-1100 hPa (from BMP280/BME280/DPS368)
  - 🔋 **Battery Monitoring**: Li-Ion voltage (2.7V-4.2V) and percentage
- **Battery Monitoring**:
  - **Hardware**: GPIO4 (ADC1_CH4) with voltage divider
  - **MOSFET Control**: GPIO3 for active power management
  - **Voltage**: Real-time battery voltage in 0.1V units
  - **Percentage**: Battery level 0-100% based on Li-Ion discharge curve
  - **Calibration**: ESP32-H2 ADC correction factor (1.604x) for accurate readings
  - **Optimized Reading**: Time-based hourly intervals with NVS persistence

#### **Endpoint 2: Rain Gauge System**
- **Hardware**: Tipping bucket rain gauge with reed switch
  - **ESP32-H2 v2.0**: GPIO12 (interrupt-capable, dedicated rain input)
- **Measurements**: Cumulative rainfall in millimeters (0.36mm per tip)
- **Features**: 
  - Interrupt-based detection (200ms debounce)
  - Persistent storage (NVS) for total tracking
  - Smart reporting (1mm threshold increments)
  - Network-aware operation (ISR enabled only when connected)
  - Works during light sleep - wakes device on rain detection
- **Specifications**: 
  - Maximum rate: 200mm/hour supported
  - Accuracy: ±0.36mm per bucket tip
  - Storage: Non-volatile total persistence across reboots
- **Use Case**: Weather station, irrigation control, flood monitoring

#### **Endpoint 3: DS18B20 External Temperature Sensor**
- **Hardware**: DS18B20 1-Wire waterproof temperature probe
  - **GPIO24**: 1-Wire bus with parasitic power support
- **Measurements**: External temperature -55°C to +125°C (±0.5°C accuracy)
- **Features**:
  - Waterproof probe for outdoor/liquid temperature monitoring
  - 12-bit resolution (0.0625°C precision)
  - Independent from I2C environmental sensors
  - Parasitic power mode (no external power needed)
- **Use Case**: Soil temperature, water temperature, outdoor ambient temperature

#### **Endpoint 4: Anemometer (Wind Speed)**
- **Hardware**: Pulse-output anemometer
  - **GPIO14**: Interrupt-capable input for pulse counting
- **Measurements**: Wind speed via pulse frequency
- **Features**:
  - Interrupt-based pulse counting with debounce
  - Persistent storage (NVS) for total tracking
  - Similar architecture to rain gauge (proven reliability)
    
#### **Endpoint 5: Wind Direction**
- **Hardware**: AS5600 magnetic rotary position sensor (I2C Bus 2)
  - **I2C Address**: 0x36 (fixed)
  - **Resolution**: 12-bit (0.087° per step)
- **Measurements**: Wind direction 0-360° (compass bearing)
- **Features**:
  - Contactless magnetic sensing (no wear)
  - Absolute position (no homing required)
  - High resolution for accurate wind vane reading

#### **Endpoint 6: Light Sensor**
- **Hardware**: VEML7700 ambient light sensor (I2C Bus 2)
  - **I2C Address**: 0x10 (fixed)
  - **Range**: 0.0036 to 120,000 lux
- **Measurements**: Ambient light intensity in lux
- **Features**:
  - High dynamic range (16-bit resolution)
  - Automatic gain adjustment
  - Human eye response matching

### 🔧 Hardware Configuration

#### **Required Components (v2.0 Hardware)**
- ESP32-H2 development board (RISC-V architecture)
- **I2C Bus 1 Sensors** (Temperature/Humidity/Pressure):
  - **SHT41** - High-accuracy temperature + humidity (recommended)
  - **AHT20** - Alternative temperature + humidity sensor
  - **BMP280** - Pressure sensor (pairs with SHT41/AHT20)
  - **BME280** - All-in-one temp/humidity/pressure (alternative)
  - **DPS368** - High-precision barometric pressure sensor (optional, alternative to BMP280)
- **I2C Bus 2 Sensors** (Wind/Light/Pressure):
  - **AS5600** - Magnetic rotary position sensor for wind direction
  - **VEML7700** - Ambient light sensor (lux measurements)
- **1-Wire Sensor**:
  - **DS18B20** - Waterproof temperature probe for external measurements
- **Weather Sensors**:
  - Tipping bucket rain gauge with reed switch (0.36mm per tip)
  - Anemometer with pulse output for wind speed measurement
- **Power System**:
  - Li-Ion battery
  - Voltage divider
  - MOSFET for battery power management (GPIO3 control)
- Zigbee coordinator (ESP32-H2 or commercial gateway)

#### **Pin Assignments**

**ESP32-H2 (v2.0 Hardware)**
```
GPIO 1  - I2C Bus 2 SDA (AS5600 wind direction, VEML7700 light sensor, DPS368 pressure)
GPIO 2  - I2C Bus 2 SCL (AS5600 wind direction, VEML7700 light sensor, DPS368 pressure)
GPIO 3  - Battery MOSFET control (active power management)
GPIO 4  - Battery voltage input (ADC1_CH4 with voltage divider)
GPIO 8  - WS2812 RGB LED (debug indicator, optional)
GPIO 9  - Built-in button (factory reset)
GPIO 10 - I2C Bus 1 SDA (SHT41/AHT20 temp/humidity, BMP280/BME280 pressure)
GPIO 11 - I2C Bus 1 SCL (SHT41/AHT20 temp/humidity, BMP280/BME280 pressure)
GPIO 12 - Rain gauge input (tipping bucket with reed switch)
GPIO 14 - Anemometer input (wind speed pulse counter)
GPIO 24 - DS18B20 1-Wire temperature sensor (waterproof probe)
```

**Note**: v2.0 uses dedicated dual I2C buses to avoid address conflicts and support expanded sensor array.

### 🔌 Zigbee Integration
- **Protocol**: Zigbee 3.0  
- **Device Type**: Sleepy End Device (SED) - maintains network connection while sleeping
- **Sleep Mode**: Automatic light sleep with 7.5s keep-alive polling
- **Sleep Threshold**: 6.0 seconds (allows sleep between keep-alive polls)
- **Supported Channels**: 11-26 (2.4 GHz)
- **Compatible**: Zigbee2MQTT, Home Assistant ZHA, Hubitat
- **OTA Support**: Over-the-air firmware updates enabled (see [OTA_GUIDE.md](OTA_GUIDE.md))

### ⚡ Power Management (Light Sleep Mode)

**Zigbee Sleepy End Device (SED) Configuration**:
- **Keep-alive Polling**: 7.5 seconds (maintains parent connection)
- **Sleep Threshold**: 6.0 seconds (enters light sleep when idle)
- **Parent Timeout**: 64 minutes (how long parent keeps device in child table)
- **RX on When Idle**: Disabled (radio off during sleep for power savings)

**Power Consumption**:
- **Sleep Current**: **0.68mA** (with RMT peripheral powered down)
- **Transmit Current**: 12mA during active transmission
- **Average Current**: ~0.83mA (includes 7.5s polling cycles)
- **LED Power-down**: Critical for low power - RMT peripheral disabled after boot

**Sleep Behavior**:
- Device sleeps automatically when Zigbee stack is idle (no activity for 6 seconds)
- Wakes every 7.5 seconds to poll parent for pending messages
- Rain detection wakes device instantly via GPIO interrupt
- Quick response to Zigbee commands (<10 seconds typical)

**Power Optimization Features**:
- RMT peripheral (LED driver) powered down after boot sequence (~1-2mA savings)
- Light sleep maintains Zigbee network association (no rejoin delays)
- Automatic sleep/wake managed by ESP Zigbee stack
- Battery monitoring on 1-hour intervals with NVS persistence
- BME280 forced measurement mode (sensor sleeps between readings)

**Battery Life Estimate**:
- **2500mAh Li-Ion**: ~125 days (4+ months) at 0.83mA average
- **Breakdown**: 
  - ~7.4 seconds sleep per 7.5 second cycle (0.68mA)
  - ~0.1 seconds active per cycle (12mA transmit)
  - Sensor readings and reports as configured

**OTA Behavior**: 
- Device stays awake during firmware updates (`esp_zb_ota_is_active()` check)
- Normal sleep resumes after OTA completion

### � Zigbee Integration
- **Protocol**: Zigbee 3.0  
- **Device Type**: End Device (sleepy end device for battery operation)
- **Supported Channels**: 11-26 (2.4 GHz)
- **Compatible**: Zigbee2MQTT, Home Assistant ZHA
- **OTA Support**: Over-the-air firmware updates enabled (see [OTA_GUIDE.md](OTA_GUIDE.md))

## 🚀 Quick Start

### Prerequisites

```bash
# Install ESP-IDF v5.5.1 or later
git clone -b v5.5.1 --recursive https://github.com/espressif/esp-idf.git
cd esp-idf
# For ESP32-H2 (v2.0 hardware required)
./install.sh esp32h2
. ./export.sh
```

### Configure the Project
```bash
# For ESP32-H2 v2.0 hardware
idf.py set-target esp32h2
idf.py menuconfig
```

**Note**: v2.0 firmware requires v2.0 hardware (dual I2C buses, new GPIO assignments). For v1.0 hardware (ESP32-C6 or ESP32-H2 single I2C), use the `caelum-weatherstation` repository.

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
  - Long press (5s): Factory reset device and rejoin network

#### **Automatic Features**
- **Light Sleep**: Automatic entry when idle (6s threshold, 7.5s keep-alive polling)
- **Sensor Reporting**: Environmental data reported after network join and on-demand
- **Rain Detection**: Interrupt-based instant wake and reporting (1mm threshold)
- **Battery Monitoring**: Hourly readings with time-based NVS persistence
- **Network Status**: Visual feedback via RGB LED during boot/join only
- **Power Savings**: RMT peripheral powered down after boot (~1-2mA savings)

### 📡 Data Reporting
- **Temperature/Humidity/Pressure**: Reported after network join and as configured
- **Rainfall**: Immediate on rain detection (1mm threshold)
- **Battery**: Hourly time-based readings with NVS persistence
- **Reporting Interval**: Configurable 60-7200 seconds via Endpoint 3
- **Response Time**: <10 seconds for Zigbee commands (7.5s keep-alive polling)

## 📊 Example Output

### Device Initialization (with Sensor Auto-Detection)
```
I (3668) i2c_bus: i2c0 bus inited
I (3670) i2c_bus: I2C Bus V2 Config Succeed, Version: 1.5.0
I (3686) i2c_bus: found i2c device address = 0x44
I (3695) i2c_bus: found i2c device address = 0x76
I (3696) SENSOR_IF: I2C scan: 2 device(s): 0x44 0x76
I (3697) SENSOR_IF: Probing for BME280...
W (3698) BME280_APP: ⚠ Detected BMP280 sensor (Chip ID: 0x58) - Temperature + Pressure ONLY (no humidity!)
I (3704) SENSOR_IF: BMP280 detected (no humidity), searching for separate humidity sensor...
I (3710) SHT41: sht41_init: device created successfully
I (3715) SENSOR_IF: Detected sensor combo: SHT41 + BMP280 (temp/RH from SHT41, pressure from BMP280)
I (3720) WEATHER_STATION: Detected SHT41 + BMP280 sensor combo on ESP32-H2 (SDA:GPIO10, SCL:GPIO11)
I (3730) RAIN_GAUGE: Rain gauge initialized. Current total: 0.00 mm
```

### Alternative Sensor Configurations
```
# BME280 all-in-one sensor
I (3697) SENSOR_IF: Probing for BME280...
I (3698) BME280_APP: ✓ Detected BME280 sensor (Chip ID: 0x60) - Temperature + Humidity + Pressure
I (3704) SENSOR_IF: Detected sensor: BME280
I (3710) WEATHER_STATION: Detected BME280 sensor on ESP32-H2 (SDA:GPIO10, SCL:GPIO11)

# SHT41 alone (no pressure sensor)
I (3697) SENSOR_IF: Probing for BME280...
I (3698) SENSOR_IF: BME280 not found, probing for SHT41 + BMP280 combo...
I (3704) SHT41: sht41_init: device created successfully
I (3710) SENSOR_IF: Detected sensor: SHT41 only (pressure will default to 1000.0 hPa)
I (3715) WEATHER_STATION: Detected SHT41 sensor on ESP32-H2 (SDA:GPIO10, SCL:GPIO11)
```

### Zigbee SED Configuration
```
I (600) WEATHER_STATION: 🔋 Configured as Sleepy End Device (SED) - rx_on_when_idle=false
I (610) WEATHER_STATION: 📡 Keep-alive poll interval: 7500 ms (7.5 sec)
I (620) WEATHER_STATION: 💤 Sleep threshold: 6000 ms (6.0 sec) - production optimized
I (630) WEATHER_STATION: ⏱️  Parent timeout: 64 minutes
I (640) WEATHER_STATION: ⚡ Power profile: 0.68mA sleep, 12mA transmit, ~0.83mA average
```

### Network Connection (LED Shows Status)
```
I (3558) WEATHER_STATION: Joined network successfully (Extended PAN ID: 74:4d:bd:ff:fe:63:f7:30, PAN ID: 0x13af, Channel:13, Short Address: 0x7c16)
I (3568) WEATHER_STATION: 💡 LED will power down in 5 seconds to save battery
I (3578) RAIN_GAUGE: Rain gauge enabled - device connected to Zigbee network
I (3588) WEATHER_STATION: 📊 Scheduling initial sensor data reporting after network join
I (8568) WEATHER_STATION: 🔌 RGB LED RMT peripheral powered down (boot sequence complete)
```

### Sensor Data Reporting
```
I (4000) WEATHER_STATION: 🌡️ Temperature: 22.35°C reported to Zigbee  
I (5010) WEATHER_STATION: 💧 Humidity: 45.20% reported to Zigbee
I (6020) WEATHER_STATION: 🌪️ Pressure: 1013.25 hPa reported to Zigbee
I (7030) BATTERY: 🔋 Li-Ion Battery: 4.17V (98%) - Zigbee values: 41 (0.1V), 196 (%*2)
I (8040) WEATHER_STATION: 📡 Temp: 22.4°C
I (8050) WEATHER_STATION: 📡 Humidity: 45.2%
I (8060) WEATHER_STATION: 📡 Pressure: 1013.3 hPa
```

### Rain Gauge Activity
```  
I (10000) RAIN_GAUGE: 🌧️ Rain pulse #1: 0.36 mm total (+0.36 mm)
I (10020) RAIN_GAUGE: ✅ Rain reported: 0.36 mm
I (10030) WEATHER_STATION: 📡 Rain: 0.36 mm
```

### Light Sleep Activity
```
I (15000) WEATHER_STATION: Zigbee can sleep
I (15010) WEATHER_STATION: 💤 Entering light sleep (will wake for 7.5s keep-alive poll)
```

## 🏠 Home Assistant Integration

When connected to Zigbee2MQTT or other Zigbee coordinators, the v2.0 device exposes:

- **6x Sensor entities**: 
  - Temperature (Environmental + DS18B20 external probe)
  - Humidity
  - Pressure  
  - Battery Percentage
  - Wind Speed
  - Wind Direction (0-360° compass bearing)
  - Light Level (lux)
- **2x Sensor entities**: 
  - Battery Voltage (mV)
  - Rainfall total (mm) with automatic updates
- **LED Control**: ⚠️ **Not available in v2.0** - LED operates only during boot/join sequence

### Endpoint Summary for Home Assistant

| Endpoint | Entity Type | Measurements | Update Frequency |
|----------|-------------|--------------|------------------|
| 1 | Sensor | Temp, Humidity, Pressure, Battery | Configurable (default 15min) |
| 2 | Sensor | Rainfall total (mm) | On rain detection (1mm threshold) |
| 3 | Sensor | External temperature (DS18B20) | Configurable (default 15min) |
| 4 | Sensor | Wind speed | Configurable (default 15min) |
| 5 | Sensor | Wind direction (0-360°) | Configurable (default 15min) |
| 6 | Sensor | Light level (lux) | Configurable (default 15min) |

### Zigbee2MQTT Integration

A custom external converter is provided for full feature support with Zigbee2MQTT. See [ZIGBEE2MQTT_CONVERTER.md](ZIGBEE2MQTT_CONVERTER.md) for:
- Converter installation instructions
- Multi-endpoint support configuration
- v2.0 endpoint mappings (6 endpoints)
- Home Assistant automation examples

**Note**: v2.0 requires updated converter due to new endpoint structure (6 endpoints vs 4 in v1.0)

## 🔄 Migration Guide: v1.0 → v2.0

### Hardware Requirements
⚠️ **v2.0 firmware requires v2.0 hardware** - GPIO assignments are completely different.

| Component | v1.0 | v2.0 | Compatible? |
|-----------|------|------|-------------|
| Target | ESP32-H2 or C6 | ESP32-H2 only | ❌ Different GPIO |
| I2C Bus | Single (GPIO10/11) | Dual (GPIO10/11 + GPIO1/2) | ⚠️ Partial |
| Rain Gauge | GPIO12 (H2), GPIO5 (C6) | GPIO12 only | ✅ H2 compatible |
| Pulse Counter | GPIO13 | Removed | ❌ Deprecated |
| Anemometer | N/A | GPIO14 | ➕ New |
| DS18B20 | N/A | GPIO24 | ➕ New |
| Battery MOSFET | N/A | GPIO3 | ➕ New |

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

**Project**: Caelum - ESP32 Zigbee Weather Station  
**Version**: v2.0 - Hardware redesign with dual I2C buses and expanded sensor array  
**Compatible**: ESP32-H2 (v2.0), ESP32-C6 (v1.0 legacy), ESP-IDF v5.5.1+  
**License**: GPL v3 (Software) / CC BY-NC-SA 4.0 (Hardware)  
**Features**: 
- 6-endpoint design (ENV, RAIN, DS18B20, WIND_SPEED, WIND_DIR, LIGHT)
- Dual I2C buses for expanded sensor support without address conflicts
- Professional weather monitoring (temperature, humidity, pressure, rain, wind, light)
- DS18B20 waterproof external temperature probe
- AS5600 magnetic wind direction sensor (0-360°)
- VEML7700 ambient light sensor (0.0036-120k lux)
- Anemometer wind speed measurement
- OTA firmware updates
- Optimized battery monitoring with hourly time-based intervals
- WS2812 RGB LED status indicator during boot/join
- Ultra-low power: 0.68mA sleep current for extended battery life

