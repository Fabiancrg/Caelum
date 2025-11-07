# LED Debug Feature Documentation

## Overview

The Caelum Weather Station includes an LED debug feature that provides visual feedback about the device's operational state. This feature is controllable via Zigbee using endpoint 4.

## Hardware

- **LED Type**: WS2812 RGB LED (on ESP32-H2 SuperMini)
- **GPIO**: GPIO8
- **Driver**: espressif/led_strip ~2.0.0

## LED States

The LED displays different colors and patterns to indicate device status:

| State | Color | Pattern | Meaning |
|-------|-------|---------|---------|
| **Disconnected** | Orange | Blinking (500ms) | Device is joining/searching for Zigbee network |
| **Connected** | Blue | Steady | Device is connected to Zigbee network |
| **Rain Detection** | White | Brief flash (100ms) | Rain gauge pulse detected |
| **Sleeping** | Off | - | Device is in deep sleep mode |
| **Disabled** | Off | - | LED debug feature disabled via Zigbee |

## Zigbee Control

### Endpoint 4: LED Debug Control

The LED debug feature can be enabled/disabled remotely via Zigbee using endpoint 4:

- **Cluster**: genOnOff (0x0006)
- **Attribute**: onOff (0x0000)
- **Device Type**: On/Off Output (0x0002)
- **Default State**: Enabled (ON)

### Usage in Zigbee2MQTT

After pairing the device with Zigbee2MQTT, a switch entity will appear for "LED Debug":

```javascript
// In Z2M frontend:
// Switch: "LED Debug" - Toggle to enable/disable LED indicator

// Via MQTT:
mosquitto_pub -t 'zigbee2mqtt/caelum-weather-station/set' -m '{"state_4":"ON"}'   // Enable LED
mosquitto_pub -t 'zigbee2mqtt/caelum-weather-station/set' -m '{"state_4":"OFF"}'  // Disable LED
```

## Implementation Details

### Code Structure

**Files Modified:**
- `main/esp_zb_weather.h` - Added endpoint 4 definition and LED configuration
- `main/esp_zb_weather.c` - Implemented LED control functions and Zigbee endpoint
- `caelum-weather-station.js` - Added endpoint 4 to Z2M converter

**Key Components:**

1. **Control Variable** (`esp_zb_weather.c`):
   ```c
   static bool led_debug_enabled = true;  // Default: enabled
   ```

2. **LED Functions**:
   - `debug_led_init()` - Initialize WS2812 LED on GPIO8
   - `debug_led_set(bool state)` - Set LED on/off (respects led_debug_enabled)
   - `debug_led_start_blink()` - Start orange blinking for network joining
   - `debug_led_stop_blink()` - Stop blinking
   - `debug_led_rain_flash()` - White flash on rain detection

3. **Zigbee Attribute Handler**:
   ```c
   // Handles genOnOff writes on endpoint 4
   // Updates led_debug_enabled and immediately applies state
   ```

4. **Endpoint Configuration**:
   ```c
   esp_zb_endpoint_config_t endpoint_led_config = {
   /* LED debug endpoint removed; On/Off moved to primary endpoint (EP1) */
       .app_profile_id = ESP_ZB_AF_HA_PROFILE_ID,
       .app_device_id = ESP_ZB_HA_ON_OFF_OUTPUT_DEVICE_ID,
   };
   ```

### Behavior

**When Enabled (ON)**:
- LED shows network status (orange blink → blue solid)
- White flash on rain gauge pulses
- Turns off during deep sleep

**When Disabled (OFF)**:
- LED immediately turns off
- All LED functions skip execution
- Rain pulse detection continues (no visual feedback)
- Network operation unaffected

### Power Considerations

The LED debug feature has minimal impact on battery life:
- Only active during wake periods (~10-60 seconds)
- Off during deep sleep (majority of time)
- RGB LED draws ~20mA when on
- Can be disabled remotely to conserve battery in production deployments

## Configuration Options

Compile-time options in `esp_zb_weather.h`:

```c
#define DEBUG_LED_ENABLE 1          // Enable LED debug feature
#define DEBUG_LED_TYPE_RGB 1        // Use WS2812 RGB LED (0 = simple GPIO)
#define DEBUG_LED_GPIO GPIO_NUM_8   // GPIO pin for LED
```

For simple GPIO LED instead of WS2812:
```c
#define DEBUG_LED_TYPE_RGB 0        // Use simple GPIO LED
```

## Troubleshooting

**LED stays off**:
- Check if feature is enabled via Zigbee (endpoint 4 state)
- Verify DEBUG_LED_ENABLE=1 in esp_zb_weather.h
- Check GPIO8 connection to WS2812 LED
- Monitor logs for initialization errors

**LED doesn't flash on rain**:
- Verify rain gauge GPIO (default GPIO4) is connected
- Check Z2M for rainfall updates
- LED must be enabled and device must be awake

**Wrong colors**:
- Verify WS2812 LED type (not WS2811 or similar)
- Check led_strip component version (~2.0.0)
- Verify RMT channel configuration

## Future Enhancements

Potential improvements:
- NVS persistence for led_debug_enabled state
- Configurable LED brightness via Zigbee
- Additional color patterns for different events
- Low battery warning (red blink)
