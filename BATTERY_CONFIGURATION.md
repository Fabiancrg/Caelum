# ESP32-H2 Battery-Powered Weather Station Guide

## Overview
This guide explains how to configure the ESP32-H2 Weather Station for battery-powered operation with deep sleep mode.

## Battery Requirements

### Recommended Battery Options

#### Option 1: 18650 Li-ion Battery (Recommended)
- **Voltage**: 3.7V nominal (3.0V - 4.2V range)
- **Capacity**: 2500-3500 mAh
- **Runtime**: 6-12 months (depending on reporting frequency)
- **Advantages**: High capacity, rechargeable, widely available
- **Cost**: $5-10 per cell

#### Option 2: 3x AA/AAA Batteries
- **Voltage**: 3x 1.5V = 4.5V nominal
- **Capacity**: 2000-3000 mAh (AA), 800-1200 mAh (AAA)
- **Runtime**: 3-6 months
- **Advantages**: Easy to replace, no special charger needed
- **Cost**: $2-5 per set

#### Option 3: LiPo Battery Pack
- **Voltage**: 3.7V (1S LiPo)
- **Capacity**: 2000-5000 mAh
- **Runtime**: 6-18 months
- **Advantages**: Lightweight, various form factors
- **Cost**: $10-20

### Battery Connection

```
ESP32-H2 Pin Configuration:
┌────────────────────┐
│     ESP32-H2       │
│                    │
│  3V3  ──┬──────────┼──── Battery + (via regulator if >3.6V)
│         │          │
│       [100µF]      │     Optional: Add 100µF capacitor
│         │          │     near power pins for stability
│  GND  ──┴──────────┼──── Battery -
│                    │
│  GPIO18 ────────────┼──── Rain Sensor (wake-up pin)
│                    │
└────────────────────┘
```

### Hardware Setup

1. **With Li-ion/LiPo (3.7V)**:
   ```
   Battery (+) ──> [TP4056 Charger] ──> [LDO 3.3V] ──> ESP32-H2 (3V3)
   Battery (-) ────────────────────────────────────> ESP32-H2 (GND)
   ```

2. **With 3x AA/AAA (4.5V)**:
   ```
   Batteries (+) ──> [AMS1117-3.3 LDO] ──> ESP32-H2 (3V3)
   Batteries (-) ─────────────────────> ESP32-H2 (GND)
   ```

## Power Consumption Analysis

### Active Mode (Zigbee TX/RX)
- **Current Draw**: 15-25 mA
- **Duration**: 2-5 seconds per wake cycle
- **Energy**: ~0.02 mAh per cycle

### Deep Sleep Mode
- **Current Draw**: 7-10 µA (microamps!)
- **Duration**: 15 minutes (900 seconds)
- **Energy**: ~0.002 mAh per cycle

### Daily Energy Calculation (15-minute intervals)
```
Wake cycles per day: 96 (24h * 4 per hour)
Active energy: 96 * 0.02 mAh = 1.92 mAh/day
Sleep energy: 96 * 0.002 mAh = 0.19 mAh/day
Total: ~2.1 mAh/day
```

### Battery Life Estimates
- **2500 mAh battery**: 2500 / 2.1 ≈ **1190 days** (~3.3 years!)
- **3000 mAh battery**: 3000 / 2.1 ≈ **1428 days** (~3.9 years!)

*Note: Actual battery life depends on self-discharge rate (~2-5% per month for Li-ion) and rain events.*

### With Rain Events
If it rains 10 times per day with 1mm+ rainfall:
```
Additional wake cycles: 10
Additional energy: 10 * 0.02 mAh = 0.2 mAh/day
New total: ~2.3 mAh/day
Battery life: still >3 years with 2500mAh battery
```

## Deep Sleep Configuration

### Wake-up Triggers

1. **Timer Wake-up (Every 15 minutes)**
   - Report temperature, humidity, pressure
   - Report rainfall total
   - Check for coordinator messages

2. **GPIO Wake-up (Rain detection)**
   - GPIO18 (rain gauge tipping bucket)
   - Wake immediately when rain detected
   - Report rainfall increment

### Sleep Mode Details

```c
// Deep sleep power domains
- RTC memory: ACTIVE (stores rainfall counter)
- RTC slow clock: ACTIVE (for wake timer)
- RTC fast memory: OFF
- Wi-Fi/Bluetooth: OFF
- Digital peripherals: OFF
- I2C: OFF (powered down)
- UART: OFF (powered down)
```

## Code Changes Made

### 1. Changed Device Type
```c
// OLD: Mains-powered End Device
#define ESP_ZB_ZED_CONFIG() ...

// NEW: Battery-powered Sleepy End Device  
#define ESP_ZB_SED_CONFIG() {
    .esp_zb_role = ESP_ZB_DEVICE_TYPE_ED,
    .install_code_policy = false,
    .nwk_cfg.zed_cfg = {
        .ed_timeout = ESP_ZB_ED_AGING_TIMEOUT_8MIN,
        .keep_alive = 15000, // 15 seconds
    },
}
```

### 2. Added Deep Sleep Functions
- `enter_deep_sleep()` - Configures and enters deep sleep
- `check_wake_reason()` - Determines why device woke up
- `handle_timer_wakeup()` - Processes periodic wake-ups
- `handle_rain_wakeup()` - Processes rain-triggered wake-ups

### 3. Modified Main Loop
- Removed infinite Zigbee loop
- Added sleep cycle after reporting
- Saves state to RTC memory

### 4. Updated Rain Gauge
- Rain detection triggers immediate wake-up
- Pulse count stored in RTC memory (survives sleep)
- Reports rainfall on wake-up

## Configuration Options

### Adjust Sleep Duration
Edit `esp_zb_weather.h`:
```c
#define SLEEP_DURATION_MINUTES  15    // Change to 10, 20, 30, etc.
#define SLEEP_DURATION_S  (SLEEP_DURATION_MINUTES * 60)
```

### Adjust Rain Threshold
```c
#define RAIN_MM_PER_PULSE  0.2794f  // Standard tipping bucket
```

### Battery Voltage Monitoring (Optional)
```c
// Add ADC code to monitor battery voltage
// Warn when battery < 3.0V for Li-ion
// Warn when battery < 3.6V for 3xAA
```

## Testing & Debugging

### Test Deep Sleep
```bash
# Monitor serial output
idf.py monitor

# You should see:
# - Device wakes up every 15 minutes
# - Reports sensor data
# - Enters deep sleep
# - Wake-up reason logged
```

### Test Rain Wake-up
```bash
# Manually trigger GPIO18 high
# Device should wake immediately
# Report rain increment
# Return to sleep
```

### Measure Current Draw
Use a multimeter in series with battery:
- Active: 15-25 mA
- Sleep: <10 µA

## LED Behavior

### Power Saving Modifications
- LED strip disabled in battery mode (saves power)
- Status LED blinks briefly on wake-up
- No continuous LEDs during sleep

## Zigbee Network Considerations

### Poll Rate
- Sleepy End Devices poll parent every 7.5 seconds while awake
- Coordinator buffers messages for sleeping devices
- Device must wake up to receive commands

### Parent Device
- **Must be connected to a Zigbee Router or Coordinator**
- Routers keep track of sleeping children
- Messages are queued until device wakes up

### Limitations
- **Cannot receive instant commands** during sleep
- Commands take effect on next wake-up (max 15 min delay)
- Coordinator must support Sleepy End Devices

## Troubleshooting

### Device doesn't wake up
- Check battery voltage (>3.0V for Li-ion)
- Verify GPIO18 connection
- Check RTC clock crystal (if external)

### Short battery life
- Verify deep sleep current (<10µA)
- Disable LED strip in code
- Increase sleep duration
- Check for wake-up loops

### Lost network connection
- Increase `ed_timeout` in config
- Reduce sleep duration
- Ensure coordinator supports long poll intervals

## Bill of Materials (BOM)

### Minimal Setup
| Component | Part Number | Qty | Cost |
|-----------|-------------|-----|------|
| ESP32-H2 DevKit | ESP32-H2-DevKitM-1 | 1 | $10 |
| 18650 Battery | Any 3.7V Li-ion | 1 | $6 |
| TP4056 Charger | TP4056 Module | 1 | $1 |
| AMS1117-3.3 | LDO Regulator | 1 | $0.50 |
| BME280 Sensor | BME280 I2C Module | 1 | $5 |
| Rain Gauge | Tipping Bucket | 1 | $15 |
| **Total** | | | **~$37** |

### Recommended Setup (with PCB)
Add:
- Custom PCB ($5)
- Battery holder ($2)
- Connector headers ($1)
- Protection circuitry ($2)
- **Total: ~$47**

## Next Steps

1. ✅ Flash the updated firmware
2. ✅ Connect battery with proper voltage regulation
3. ✅ Test wake-up behavior
4. ✅ Monitor current consumption
5. ✅ Pair with Zigbee coordinator
6. ✅ Verify 15-minute reporting
7. ✅ Test rain detection wake-up
8. ✅ Calculate actual battery life

---
**Important**: Always use proper voltage regulation. Direct connection of >3.6V to ESP32-H2 will damage the chip!
