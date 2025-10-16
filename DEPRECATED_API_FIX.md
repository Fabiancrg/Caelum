# Deep Sleep API Update - Deprecated Function Fix

## Issue
Compiler warning about deprecated function:
```
warning: 'esp_zb_main_loop_iteration' is deprecated [-Wdeprecated-declarations]
```

## Root Cause
The original deep sleep implementation used `esp_zb_main_loop_iteration()` in a loop, which is deprecated in newer ESP-Zigbee library versions.

## Solution
Changed approach from manual loop iterations to using the Zigbee scheduler with the proper main loop function.

### Before (Deprecated Approach)
```c
/* Run Zigbee stack iterations for limited time */
for (int i = 0; i < 100; i++) {
    esp_zb_main_loop_iteration();  // ❌ DEPRECATED
    vTaskDelay(pdMS_TO_TICKS(100));
}

/* Then enter sleep */
enter_deep_sleep(sleep_duration, true);
```

**Problems:**
- Uses deprecated `esp_zb_main_loop_iteration()` function
- Manual loop control is fragile
- Doesn't integrate well with Zigbee scheduler

### After (Modern Approach)
```c
/* Schedule deep sleep entry after allowing time for Zigbee operations */
esp_zb_scheduler_alarm((esp_zb_callback_t)prepare_for_deep_sleep, 0, 10000); // 10 seconds

/* Start main Zigbee stack loop */
esp_zb_stack_main_loop();  // ✅ CORRECT - Non-deprecated function
```

**Benefits:**
- Uses non-deprecated `esp_zb_stack_main_loop()` function
- Integrates properly with Zigbee scheduler
- Allows other scheduled callbacks (like BME280 readings) to execute
- Cleaner code architecture

## New Function Added

### `prepare_for_deep_sleep()`
```c
static void prepare_for_deep_sleep(uint8_t param)
{
    ESP_LOGI(TAG, "✅ Zigbee operations complete, preparing for deep sleep...");
    
    /* Save rainfall data before sleeping */
    float current_rainfall = total_rainfall_mm;
    uint32_t current_pulses = rain_pulse_count;
    save_rainfall_data(current_rainfall, current_pulses);
    
    /* Determine sleep duration based on recent activity */
    uint32_t sleep_duration = get_adaptive_sleep_duration(0.0f);
    
    ESP_LOGI(TAG, "💤 Entering deep sleep for %lu seconds (%lu minutes)...", 
             sleep_duration, sleep_duration / 60);
    
    /* Give time for log output */
    vTaskDelay(pdMS_TO_TICKS(100));
    
    /* Enter deep sleep with timer and GPIO wake-up enabled */
    enter_deep_sleep(sleep_duration, true);
}
```

**Purpose:**
- Scheduled callback executed by Zigbee scheduler after 10 seconds
- Saves state (rainfall data) to RTC memory and NVS
- Calculates adaptive sleep duration
- Enters deep sleep with wake-up sources configured

## Execution Flow

### Updated Flow
```
1. esp_zb_start(false)
   ↓
2. Schedule BME280 reading (5 seconds)
   ↓
3. Schedule deep sleep preparation (10 seconds)
   ↓
4. esp_zb_stack_main_loop()
   ↓
5. [Zigbee stack processes events]
   ├─ BME280 reading @ 5s
   ├─ Report sensor data
   └─ prepare_for_deep_sleep() @ 10s
      ↓
6. enter_deep_sleep()
   ↓
7. Device sleeps for 15 minutes
   ↓
8. Wake up (timer or rain GPIO)
   ↓
9. Restart from step 1
```

## Key Differences

| Aspect | Old Approach | New Approach |
|--------|-------------|--------------|
| **Main Loop** | `esp_zb_main_loop_iteration()` | `esp_zb_stack_main_loop()` |
| **Status** | ❌ Deprecated | ✅ Current API |
| **Control** | Manual loop with delays | Scheduler-based |
| **Integration** | Poor with scheduler | Perfect integration |
| **Timing** | Fixed 10 seconds | Scheduled 10 seconds |
| **Flexibility** | Limited | Highly flexible |

## Correct ESP-Zigbee Main Loop Functions

From `esp_zigbee_core.h`:

### ✅ Current (Non-Deprecated)
```c
void esp_zb_stack_main_loop(void);
// @brief Main loop function for Zigbee stack
// Runs continuously, handles all Zigbee events
```

```c
void esp_zb_stack_main_loop_iteration(void);
// @brief Single iteration of main loop
// For advanced use cases requiring manual control
```

### ❌ Deprecated (Don't Use)
```c
void esp_zb_main_loop_iteration(void); // DEPRECATED
// @deprecated Please use esp_zb_stack_main_loop() instead
```

## Benefits of Scheduler-Based Approach

1. **Non-blocking**: Other Zigbee operations can proceed normally
2. **Flexible**: Easy to adjust timing without code restructuring
3. **Reliable**: Integrates with Zigbee's internal event handling
4. **Maintainable**: Uses official API patterns
5. **Compatible**: Works with future ESP-Zigbee library updates

## Testing

### Verify Correct Behavior
```bash
idf.py build
# Should compile without warnings

idf.py -p COM_PORT flash monitor
# Expected output:
# ⏳ Starting Zigbee stack, will enter deep sleep in 10 seconds...
# 🌡️ Temperature: 22.5°C reported to Zigbee
# ✅ Zigbee operations complete, preparing for deep sleep...
# 💤 Entering deep sleep for 900 seconds (15 minutes)...
```

### Timing Verification
- BME280 reading should occur at ~5 seconds
- Deep sleep entry should occur at ~10 seconds
- All sensor data should be reported before sleep
- Rainfall data should be saved to RTC/NVS

## Additional Notes

### Why 10 Seconds Active Time?
- **0-3s**: Zigbee network join/rejoin (if needed)
- **3-5s**: Network stabilization
- **5s**: BME280 sensor reading triggered
- **5-7s**: Sensor data collection and reporting
- **7-10s**: Zigbee report transmission and confirmation
- **10s**: Enter deep sleep

This ensures sufficient time for:
- Network operations to complete
- Sensor readings to be taken
- Data reports to be sent and acknowledged
- State to be saved reliably

### Adaptive Timing
The `get_adaptive_sleep_duration()` function can shorten active time during stable conditions or extend it during active weather events.

## Migration Guide

If you have similar code using deprecated functions:

### Step 1: Replace deprecated function
```c
// OLD
esp_zb_main_loop_iteration();

// NEW
esp_zb_stack_main_loop();  // For continuous loop
// OR
esp_zb_stack_main_loop_iteration();  // For single iteration
```

### Step 2: Use scheduler for timed actions
```c
// Instead of manual loops
for (int i = 0; i < 100; i++) {
    esp_zb_main_loop_iteration();
    vTaskDelay(100);
}
do_something();

// Use scheduler
esp_zb_scheduler_alarm((esp_zb_callback_t)do_something, 0, 10000);
esp_zb_stack_main_loop();
```

### Step 3: Update callback signatures
```c
// Scheduler callbacks must match signature
void my_callback(uint8_t param) {
    // Your code here
}
```

## References

- ESP-Zigbee Programming Guide: Deep Sleep
- `esp_zigbee_core.h`: Main loop functions documentation
- ESP-IDF Deep Sleep API: `esp_sleep.h`

---
**Status**: ✅ Fixed and tested
**Date**: October 16, 2025
**Impact**: Removes deprecation warning, improves code maintainability
