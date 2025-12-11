hy [![Support me on Ko-fi](https://ko-fi.com/img/githubbutton_sm.svg)](https://ko-fi.com/Fabiancrg)

| Supported Targets | ESP32-C6 | ESP32-H2 |
| ----------------- |  -------- | -------- |

# Caelum - Zigbee Weather Station

[![License: GPL v3](https://img.shields.io/badge/Software-GPLv3-blue.svg)](./LICENSE)
[![License: CC BY-NC-SA 4.0](https://img.shields.io/badge/Hardware-CC%20BY--NC--SA%204.0-green.svg)](./LICENSE-hardware)

## Project Description

This project implements a battery-powered environmental monitoring device using ESP32-C6/H2 with Zigbee connectivity. The device operates as a Zigbee Sleepy End Device (SED) with automatic light sleep for ultra-low power consumption while maintaining network responsiveness. Features a 3-endpoint design with WS2812 RGB LED boot indicator and optimized power management achieving 0.68mA sleep current.

This project is based on the examples provided in the ESP Zigbee SDK:

* [ESP Zigbee SDK Docs](https://docs.espressif.com/projects/esp-zigbee-sdk)
* [ESP Zigbee SDK Repo](https://github.com/espressif/esp-zigbee-sdk)

## Device Features

### 🌐 Zigbee Endpoints Overview

| Endpoint | Device Type | Clusters | Description |
|----------|-------------|----------|-------------|
| **1** | Environmental Sensor | Temperature, Humidity, Pressure, Battery, OTA | BME280 sensor via I2C |
| **2** | Rain Gauge | Analog Input | Tipping bucket rain sensor with rainfall totals |
| **3** | Sleep Configuration | Analog Input | Remote sleep duration control (60-7200 seconds) |
| **4 (removed)** | LED Debug Control | On/Off | ⚠️ **LED now always active during boot/join only** - shows network status (yellow blink → blue success / red failure), then powers down permanently to save battery |

### 💡 LED Boot Indicator (Always Active)

The device includes a WS2812 RGB LED on GPIO8 (ESP32-H2) that provides visual feedback during boot and network join:
- **Yellow (Blinking)**: Joining/searching for network  
- **Blue (Steady, 5 seconds)**: Successfully connected to Zigbee network
- **Red (Blinking, 10 times)**: Connection failed after max retries
- **Off**: LED powered down after boot sequence (RMT peripheral disabled for power savings)

**Power Optimization**: The LED and its RMT peripheral are permanently disabled after the initial boot/join sequence to save ~1-2mA. This is critical for achieving 0.68mA sleep current. The LED cannot be re-enabled remotely - it only operates during the first boot cycle.

### 📋 Detailed Endpoint Descriptions

#### **Endpoint 1: Environmental Monitoring & Power Management**
- **Hardware**: Environmental sensors via I2C (auto-detection)
  - **ESP32-H2**: SDA: GPIO10, SCL: GPIO11
  - **ESP32-C6**: SDA: GPIO6, SCL: GPIO7
- **Supported Sensors** (automatically detected):
  - **BME280**: Temperature + Humidity + Pressure (all-in-one sensor)
  - **BMP280**: Temperature + Pressure only (no humidity sensor)
  - **SHT40/SHT41**: High-accuracy Temperature + Humidity (no pressure)
  - **SHT41 + BMP280**: Combined setup (temp/humidity from SHT41, pressure from BMP280)
  - **AHT20 + BMP280**: Alternative combo (temp/humidity from AHT20, pressure from BMP280)
- **Measurements**: 
  - 🌡️ **Temperature**: -40°C to +85°C (±0.2°C accuracy with SHT40, ±1°C with BME280/BMP280)
  - 💧 **Humidity**: 0-100% RH (±1.8% accuracy with SHT40, ±3% with BME280, unavailable with BMP280 alone) 
  - 🌪️ **Pressure**: 300-1100 hPa (±1 hPa accuracy with BME280/BMP280, default 1000 hPa with SHT40 alone)
  - 🔋 **Battery Monitoring**: Li-Ion voltage (2.7V-4.2V) and percentage
- **Sensor Detection**: Automatic chip ID detection distinguishes BME280 (0x60) from BMP280 (0x58)
  - BMP280 detected → searches for SHT40/41 for humidity readings
  - Optimal combo: SHT40 (accurate temp/humidity) + BMP280 (pressure)
- **Battery Monitoring**:
  - **Hardware**: GPIO4 (ADC1_CH4) with voltage divider (2x 100kΩ resistors)
  - **Voltage**: Real-time battery voltage in 0.1V units
  - **Percentage**: Battery level 0-100% based on Li-Ion discharge curve
  - **Calibration**: ESP32-H2 ADC correction factor (1.604x) for accurate readings
  - **Power Configuration Cluster**: Standard Zigbee battery attributes
  - **Optimized Reading**: 
    - Time-based hourly intervals (3600 seconds) with NVS persistence
    - 3 ADC samples for accurate readings with minimal power use
    - Always reports on first boot/pairing, then hourly regardless of wake frequency
- **Features**: Automatic reporting via Zigbee attribute updates, Zigbee-standard units
- **Use Case**: Weather monitoring, HVAC automation, air quality tracking, battery-powered applications

#### **Endpoint 2: Rain Gauge System**
- **Hardware**: Tipping bucket rain gauge with reed switch
  - **ESP32-H2**: GPIO12 (interrupt-capable)
  - **ESP32-C6**: GPIO5 (interrupt-capable)
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
  - Power: Minimal impact - GPIO interrupt wakes from light sleep
- **Use Case**: Weather station, irrigation control, flood monitoring

#### **Endpoint 3: Sleep Configuration**
- **Hardware**: Software-only (virtual endpoint)
- **Control**: Sleep duration in seconds (60-7200s range)
- **Features**:
  - Remote configuration via Zigbee2MQTT or Home Assistant
  - Persistent storage (NVS) - survives reboots
  - Real-time updates - changes apply immediately
  - Default: 900 seconds (15 minutes)
- **Note**: In light sleep mode (current implementation), this controls sensor reporting interval rather than actual sleep duration. Device maintains network connection and wakes every 7.5 seconds for keep-alive polling.
- **Use Case**: 
  - Battery optimization (longer intervals = less frequent sensor readings)
  - Seasonal adjustments (frequent updates in rainy season)
  - Dynamic reporting based on weather conditions

#### **Endpoint 4: LED Debug Control (Deprecated)**
- **Status**: ⚠️ **No longer controllable** - LED now operates only during boot/join sequence
- **Previous Function**: Remote LED control via Zigbee2MQTT
- **Current Behavior**: LED automatically shows boot status (yellow → blue/red) then powers down permanently
- **Reason**: RMT peripheral consumes 1-2mA continuously. Power-down is essential for 0.68mA sleep current.
- **Migration**: Remove any automation/scripts that attempt to control LED endpoint
- **Documentation**: See code comments in `esp_zb_weather.c` for LED implementation details

### 🔧 Hardware Configuration

#### **Required Components**
- ESP32-C6 or ESP32-H2 development board
- **Environmental Sensor** (one of the following):
  - **BME280** - All-in-one temperature + humidity + pressure sensor
  - **BMP280** - Temperature + pressure only (pair with SHT40/41 for humidity)
  - **SHT40 or SHT41** - High-accuracy temperature + humidity (pair with BMP280 for pressure)
  - **AHT20** - Temperature + humidity alternative (pair with BMP280 for pressure)
- Tipping bucket rain gauge with reed switch
- Li-Ion battery (with protection circuit, 5V output recommended)
- Voltage divider (2x 100kΩ resistors for battery monitoring)
- Zigbee coordinator (ESP32-H2 or commercial gateway)

**Recommended Sensor Combinations**:
- **Best accuracy**: SHT40 + BMP280 (temp/humidity from SHT40, pressure from BMP280)
- **All-in-one**: BME280 (good all-around accuracy, single module)
- **Budget**: BMP280 alone (temperature + pressure, no humidity)

#### **Pin Assignments**

**ESP32-H2 (Recommended)**
```
GPIO 4  - Battery voltage input (ADC1_CH4 with voltage divider)
GPIO 8  - WS2812 RGB LED (debug indicator, optional)
GPIO 9  - Built-in button (factory reset)
GPIO 10 - I2C SDA (environmental sensors: BME280/BMP280/SHT40/SHT41/AHT20)
GPIO 11 - I2C SCL (environmental sensors: BME280/BMP280/SHT40/SHT41/AHT20) 
GPIO 12 - Rain gauge input (RTC-capable)*
```

**ESP32-C6**
```
GPIO 4  - Battery voltage input (ADC1_CH4 with voltage divider)
GPIO 5  - Rain gauge input (RTC-capable)*
GPIO 6  - I2C SDA (environmental sensors: BME280/BMP280/SHT40/SHT41/AHT20)
GPIO 7  - I2C SCL (environmental sensors: BME280/BMP280/SHT40/SHT41/AHT20) 
GPIO 9  - Built-in button (factory reset)
```

*Both targets now use RTC-capable GPIO pins for rain detection during deep sleep

**Battery Voltage Divider Circuit**
```
Battery+ ──┬── 100kΩ ──┬── 100kΩ ── GND
           │           │
           │           └── GPIO4 (ADC input)
           │
           └── 5V output (to ESP32 power)
```
```
Note: Voltage divider monitors the cell voltage (2.7V-4.2V) while the battery pack provides 5V regulated output

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
- **Compatible**: Zigbee2MQTT, Home Assistant ZHA, Hubitat
- **OTA Support**: Over-the-air firmware updates enabled (see [OTA_GUIDE.md](OTA_GUIDE.md))

### ⚡ Power Management
- **Deep Sleep Mode**: Configurable intervals (60-7200 seconds, default 15 minutes)
  - **Configurable via Zigbee2MQTT**: Adjust sleep duration remotely from Home Assistant
  - **Connected**: Uses configured interval (default 15 minutes)
  - **Disconnected**: 30-second intervals for quick reconnection
  - **Max retries**: After 20 attempts (10 minutes), reduces to half configured duration
  - **Extended wake time**: Device stays awake for 60 seconds when not connected (instead of 10s) to allow Zigbee join process to complete
- **Wake-up Sources**:
  - Timer (configurable 60-7200 seconds)
  - **Rain detection during deep sleep** (both targets support RTC wake-up)
    - **ESP32-H2**: GPIO12 (RTC-capable)
    - **ESP32-C6**: GPIO5 (RTC-capable)
- **Network Retry Logic**: 
  - Prevents deep sleep when not connected to ensure reliable pairing
  - Extended 60-second wake period for join process completion
  - Connection success triggers 15-second reporting window before sleep
- **Battery Life**: Optimized for extended operation on battery power
  - **Total Power Consumption**: ~230µAh/day (optimized from ~700µAh/day)
  - **Battery monitoring**: ~12µAh/day (hourly readings with time-based intervals)
  - **Deep sleep**: 7-10µA baseline
  - **Active time**: ~100mA during 15-minute wake cycles
- **Battery Estimate**: 
  - **2500mAh battery**: ~10.9 years (optimized from ~3.5 years)
  - **Optimization breakdown**: 
    - 3 ADC samples instead of 10 (70% ADC power savings)
    - No delays between samples (90% overhead reduction)
    - Time-based hourly intervals (vs. every wake cycle)
    - Total battery monitoring: 98% power reduction

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

When connected to Zigbee2MQTT or other Zigbee coordinators, the device appears as:

- **4x Sensor entities**: Temperature, Humidity, Pressure, Battery Percentage
- **1x Sensor entity**: Battery Voltage (mV)
- **1x Sensor entity**: Rainfall total with automatic updates
- **1x Number entity**: Sleep duration control (60-7200 seconds)
- **LED Control**: ⚠️ **No longer available** - LED operates only during boot/join sequence

### Zigbee2MQTT Integration

A custom external converter is provided for full feature support with Zigbee2MQTT. See [ZIGBEE2MQTT_CONVERTER.md](ZIGBEE2MQTT_CONVERTER.md) for:
- Converter installation instructions
- Multi-endpoint support configuration
- Sleep duration control setup
- Home Assistant automation examples

### Device Information
- **Manufacturer**: ESPRESSIF
- **Model**: caelum
- **Firmware Version**: Managed via CMakeLists.txt (PROJECT_VER, BUILD_NUMBER)
  - Generated header approach using `version.h.in` template
  - Single source of truth for all version macros
  - Automatically propagated to Zigbee Basic cluster attributes
- **Supported**: Zigbee2MQTT with custom external converter (`caelum-weather-station.js`)

## 🔧 Configuration

### Version Management

Version information is centrally managed in `CMakeLists.txt`:

```cmake
set(PROJECT_VER "1.0")           # Major.Minor version
set(BUILD_NUMBER 0)              # Build/patch number
```

The build system automatically:
- Generates `build/generated/version.h` from `version.h.in` template
- Populates all version macros (FW_VERSION, FW_DATE_CODE, OTA_FILE_VERSION, etc.)
- Propagates to Zigbee Basic cluster (swBuildId, dateCode, applicationVersion)
- Updates OTA cluster attributes for firmware update tracking

### Application Configuration

**Zigbee SED Configuration** (in `main/esp_zb_weather.c`):
- **Keep-alive interval**: 7500ms (7.5 seconds) - Zigbee parent polling
- **Sleep threshold**: 6000ms (6.0 seconds) - idle time before light sleep
- **Parent timeout**: 64 minutes - how long parent keeps device in child table
- **Power profile**: 0.68mA sleep, 12mA transmit, ~0.83mA average

**Device Parameters** (in `main/esp_zb_weather.h`):
- **Default sleep duration**: 900 seconds (15 minutes) - configurable via Endpoint 3
- **Sleep duration range**: 60-7200 seconds (1 minute to 2 hours)
- **Rain tip bucket volume**: 0.36mm per tip
- **Battery monitoring interval**: 3600 seconds (1 hour) - time-based with NVS persistence
- **Battery ADC samples**: 3 samples averaged for accuracy
- **LED boot indicator**: WS2812 RGB on GPIO8 (ESP32-H2), automatically powered down after boot
- **Network retry**: 20 max retries when not connected
- **Connection timeout**: Extended wake time during network join

## 📝 Project Structure

```
WeatherStation/
├── main/
│   ├── esp_zb_weather.c     # Main Zigbee stack, 4-endpoint logic, network retry, LED control
│   ├── esp_zb_weather.h     # Configuration, endpoint definitions, LED settings
│   ├── esp_zb_ota.c         # OTA update implementation
│   ├── esp_zb_ota.h         # OTA interface
│   ├── sleep_manager.c      # Deep sleep management with RTC GPIO support
│   ├── sleep_manager.h      # Sleep manager interface
│   ├── bme280_app.c         # BME280 sensor driver with board-specific I2C
│   ├── bme280_app.h         # BME280 interface
│   ├── weather_driver.c     # DEPRECATED: Legacy driver (unused)
│   └── weather_driver.h     # DEPRECATED: Legacy interface (unused)
├── Doc/
│   └── README_GIT.md        # Git workflow guide for team (Azure DevOps)
├── caelum-weather-station.js # Zigbee2MQTT external converter (4 endpoints)
├── version.h.in             # Version header template (for configure_file)
├── CMakeLists.txt           # Build configuration with version generation
├── partitions.csv           # Partition table with OTA support
├── sdkconfig.defaults       # Default SDK settings
├── OTA_GUIDE.md            # OTA update instructions
├── LED_DEBUG_FEATURE.md    # LED debug feature documentation
└── README.md               # This file
```

## �️ Recent changes (summary)

The following changes were applied to the firmware and Zigbee2MQTT converter in recent updates. This section documents the edits made so you can rebuild, flash and test the updated behaviour.

- Converter (`caelum-weather-station.js`):
  - Mapped the device to advertise endpoints 1..3 (removed explicit EP4 advertisement).
  - Switched configureReporting for analog inputs to numeric reporting values (example: {min:10, max:3600, change:0.1}) so controllers only subscribe to the `presentValue` attribute and not `statusFlags`.
  - Moved the On/Off expose to the primary endpoint mapping so controllers will use EP1 for LED control.

- Firmware (`main/esp_zb_weather.c`, `main/esp_zb_weather.h` and related):
  - Endpoint & cluster changes:
    - Removed Basic cluster exposure from non-primary endpoints to prevent coordinators reading Basic attributes on the sensor endpoints.
    - Moved `genOnOff` (LED debug) to the primary endpoint (EP1); no separate EP4 is registered anymore.
    - Ensured Analog Input clusters expose only `presentValue` for rain / sleep configuration to avoid sending `statusFlags` to the controller.
    - Fixed ZCL string length prefixing for `sleep_description` and other length-prefixed ZCL string attributes.

  - Rain gauge reliability and counting fixes:
    - ISR now timestamps events and queues a small event struct so the rain task can disambiguate wake-count vs queued ISR events.
    - Introduced a small duplicate-suppression window after wake (50 ms) to avoid double-counting the same physical tip when the wake path and ISR both fire for the same edge.
    - Kept the ISR handler installed across sleep/wake; bounce handling now disables/re-enables the GPIO interrupt line (gpio_intr_disable/enable) instead of removing and re-adding the ISR handler. This reduces the race window where pulses could be missed.
    - Reduced the bounce settle time and debounce defaults to improve responsiveness while still preventing false tips (defaults in code: 200 ms debounce / 200 ms settle).
    - Queue events carry ISR ticks for robust timing comparisons in the consumer task.

  - Wake/reporting timing and sleep behaviour:
    - The device now schedules a shorter reporting window after wake so overall awake time is reduced.
      - Reporting window (time between wake and starting deep-sleep preparation) is now 7 seconds (was previously 15s / 10s in some paths).
      - Per-sensor scheduled report offsets inside that window: BME280 at ~1s, rain update at ~2s, battery at ~3s, sleep-duration at ~4s.
    - Final delays in `prepare_for_deep_sleep()` were reduced (smaller vTaskDelay values) while preserving a short transmission window to allow Zigbee packets to be sent.
    - OTA behaviour is unchanged: if an OTA update is active the device will postpone deep sleep and periodically re-check the OTA status until the transfer is complete.

  - Miscellaneous:
    - `RAIN_MM_PER_PULSE` remains 0.36 mm per tip; all rain math still uses this constant.
    - NVS persistence for rain totals and sleep configuration remains in place and is used to recover counts across reboots.

## 🔁 How to build & flash the updated firmware (Windows PowerShell)

Use your existing ESP-IDF environment and the usual build/flash commands. Example (PowerShell):

```powershell
# Set target and open menuconfig if needed
idf.py set-target esp32h2
idf.py menuconfig

# Optional: erase flash for a clean install
idf.py -p COM3 erase-flash

# Build and flash
idf.py -p COM3 flash monitor
```

Replace `COM3` with your serial port. `monitor` attaches the serial monitor after flash; you'll see the device logs in the console.

## ✅ Test checklist after flashing

1. Pair the device with your Zigbee coordinator (or let Zigbee2MQTT re-discover the device).
2. Verify endpoints: device should advertise endpoints 1..3 and the LED On/Off should be on EP1.
3. Check Zigbee2MQTT logs: ensure that only `presentValue` for genAnalogInput is reported for rain and sleep duration, and that `statusFlags` is not being reported.
4. Trigger a rain pulse (or simulate the reed switch): verify that pulses are counted even if they occur before Zigbee reconnect; verify duplicate suppression does not double-count the same physical tip.
5. Observe a normal wake cycle: sensor reports should appear at ~1s/2s/3s/4s and the device should enter deep sleep after ~7s (log messages show the exact timing).
6. Start an OTA update (if available) and verify the device stays awake for the duration of the transfer.

If any reports are missed in practice, increase the reporting window to 10s or increase the final transmission delay in `prepare_for_deep_sleep()`.

If you'd like, I can prepare a complementary small test patch that logs timestamps when each scheduled report runs so you can measure precise timings on your hardware.


## �🔧 Troubleshooting

### Common Issues

#### **Rain Gauge Not Detecting**
- **ESP32-H2**: Verify GPIO12 connections and reed switch operation
- **ESP32-C6**: Verify GPIO5 connections and reed switch operation
- Check that device is connected to Zigbee network (rain gauge only active when connected)
- Ensure proper pull-down resistor on rain gauge input
- Both targets support rain detection during deep sleep

#### **BME280 Not Reading**  
- **ESP32-H2**: Check I2C connections (SDA: GPIO10, SCL: GPIO11)
- **ESP32-C6**: Check I2C connections (SDA: GPIO6, SCL: GPIO7)
- Verify BME280 I2C address (default: 0x76 or 0x77)
- Ensure proper power supply to sensor (3.3V)

#### **Battery Monitoring Issues**
- Verify voltage divider connections (2x 100kΩ resistors to GPIO4)
- Check battery cell voltage tap (should be 2.7V-4.2V at divider output)
- Ensure GPIO4 (ADC1_CH4) is properly configured
- Monitor serial output for ADC calibration messages
- **ESP32-H2 ADC quirk**: Uses empirical correction factor (1.604x) for DB_12 attenuation
- Battery percentage calculated from voltage (2.7V=0%, 4.2V=100%)
- **Optimized reading schedule**: 
  - First reading always happens on boot/pairing
  - Subsequent readings every hour (3600 seconds) based on elapsed time
  - Uses NVS to persist timestamp across deep sleep cycles
  - Robust to frequent wake-ups (e.g., during rain events)

#### **Zigbee Connection Issues**
- Perform factory reset with long press (5s) on built-in button
- Ensure Zigbee coordinator is in pairing mode
- Check channel compatibility between coordinator and device
- **Auto-retry**: Device automatically retries every 30 seconds when disconnected
- **Extended wake time**: Device now stays awake for 60 seconds (instead of 10s) during join attempts
- **Battery impact**: Extended connection attempts may drain battery faster
- **Join timing**: Allow full 60 seconds for device to complete Zigbee join process

#### **Device Not Waking from Sleep**
- Check battery voltage (minimum 3.0V recommended)
- Verify wake-up sources (timer, rain gauge)
- Review sleep manager logs for errors
- **Network dependency**: Device stays awake longer when not connected to network

#### **High Battery Drain**
- Check if device is stuck in connection retry mode (30-second intervals)
- Verify Zigbee network is stable and accessible
- Monitor connection retry count in device logs
- Consider factory reset if connection issues persist
- **Battery monitoring optimization**: Should consume only ~12µAh/day (hourly readings)
- **Total optimized power**: ~230µAh/day → 10.9 year battery life (2500mAh battery)
- **LED Debug**: If enabled, LED consumes ~20mA during wake cycles (can be disabled via Zigbee)

#### **LED Not Working**
- **ESP32-H2 SuperMini**: Verify GPIO8 connection to WS2812 RGB LED
- Check LED debug feature is enabled in `esp_zb_weather.h` (`DEBUG_LED_ENABLE=1`)
- Verify LED debug is enabled via Zigbee2MQTT switch (endpoint 4)
- Monitor serial output for LED initialization messages
- Ensure led_strip component (~2.0.0) is properly installed
- **LED States**: Blue=connected, Orange blink=joining, White flash=rain, Off=sleep/disabled
- See [LED_DEBUG_FEATURE.md](LED_DEBUG_FEATURE.md) for detailed troubleshooting

### 📋 Development Notes

- **ESP-IDF Version**: v5.5.1 recommended
- **Zigbee SDK**: Latest ESP Zigbee SDK required  
- **Memory Usage**: ~2MB flash, ~200KB RAM typical
- **Power Consumption**: 
  - Active: ~100mA
  - Deep sleep: 7-10µA
  - Battery monitoring: ~12µAh/day (optimized)
  - **Total**: ~230µAh/day (3.1× improvement from ~700µAh/day)
- **Battery Operation**: Optimized for CR123A or Li-ion battery packs
  - **2500mAh battery**: ~10.9 years (vs. 3.5 years before optimization)
- **Target Support**: Both ESP32-H2 and ESP32-C6 fully support RTC GPIO wake-up for rain detection
- **Version Management**: Centralized in CMakeLists.txt with generated header approach

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

This project is derived from ESP32 Zigbee SDK examples and implements a 4-endpoint architecture optimized for battery-powered weather station applications with:
- Remote sleep duration configuration (Endpoint 3)
- LED debug status indicator with remote control (Endpoint 4)
- Deep sleep power management with network retry logic
- Extended wake time during Zigbee join process (60 seconds)
- RTC GPIO wake-up support for rain detection on both ESP32-H2 and ESP32-C6
- Optimized battery monitoring with 98% power reduction

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
**Version**: Managed via CMakeLists.txt (PROJECT_VER, BUILD_NUMBER)  
**Compatible**: ESP32-C6, ESP32-H2, ESP-IDF v5.5.1+  
**License**: GPL v3 (Software) / CC BY-NC-SA 4.0 (Hardware)  
**Features**: 
- 4-endpoint design (sensors, rain, sleep config, LED debug)
- Remote sleep configuration and LED control via Zigbee
- OTA firmware updates
- RTC GPIO wake-up for rain detection
- Optimized battery monitoring (~98% power reduction)
- Time-based hourly intervals with NVS persistence
- WS2812 RGB LED status indicator (optional)
- 10.9 year battery life (2500mAh, 15-min intervals)

