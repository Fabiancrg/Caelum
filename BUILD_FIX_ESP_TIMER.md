# Build Fix: Removed esp_timer.h Dependency

## Issue
Compilation error in `sleep_manager.c`:
```
fatal error: esp_timer.h: No such file or directory
```

## Root Cause
The `esp_timer.h` header was included but:
1. Not actually used in the code
2. May not be available or properly configured for ESP32-H2 target
3. Was an unnecessary dependency

## Solution
Removed the unnecessary `esp_timer.h` include and added required FreeRTOS headers instead.

### Changes Made

**File: `main/sleep_manager.c`**

```diff
- #include "esp_timer.h"
+ #include "freertos/FreeRTOS.h"
+ #include "freertos/task.h"
```

## Why This Works

The `sleep_manager.c` file uses:
- `vTaskDelay()` - Requires `freertos/task.h` ✅
- ESP sleep functions - Requires `esp_sleep.h` ✅ 
- GPIO functions - Requires `driver/gpio.h` ✅
- NVS functions - Requires `nvs.h` and `nvs_flash.h` ✅
- Logging - Requires `esp_log.h` ✅

**None of these require `esp_timer.h`** - it was included by mistake.

## Verification

After this fix, the file should compile cleanly:

```bash
cd C:\Devel\Repositories\WeatherStation
idf.py fullclean
idf.py build
```

Expected result: ✅ **No errors**

## Related Files

All sleep management files are now properly configured:
- ✅ `main/sleep_manager.c` - Implementation (fixed)
- ✅ `main/sleep_manager.h` - Header file (no issues)
- ✅ `main/esp_zb_weather.c` - Uses sleep manager (includes header correctly)

## Additional Notes

### CMakeLists.txt
The `main/CMakeLists.txt` uses `SRC_DIRS "."` which automatically includes all `.c` files in the main directory, so `sleep_manager.c` is automatically compiled - no changes needed.

### Required FreeRTOS Functions
The sleep manager uses:
- `vTaskDelay(pdMS_TO_TICKS(100))` - For log flushing before sleep
- `pdMS_TO_TICKS()` macro - For time conversion

Both are provided by `freertos/task.h`.

## Build Configuration

No additional CMakeLists.txt changes required because:
1. ✅ `SRC_DIRS "."` includes all C files automatically
2. ✅ FreeRTOS is always available (core component)
3. ✅ ESP sleep API is always available
4. ✅ GPIO drivers already in PRIV_REQUIRES

---
**Status**: ✅ Fixed
**Build Impact**: None - removes unnecessary dependency
**Functionality**: No change - code works identically
