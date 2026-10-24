# Installation

## PlatformIO

Add to your `platformio.ini`:

```ini
[env:myproject]
lib_deps =
    mathieucarbou/MycilaTaskManager@^4.2.5
```

## Arduino IDE

1. Go to **Sketch** → **Include Library** → **Manage Libraries**
2. Search for "MycilaTaskManager"
3. Install the library by Mathieu Carbou

## Dependencies

- **ESP32 Arduino Core** — the library relies on FreeRTOS (`freertos/FreeRTOS.h`, `freertos/task.h`), `esp_task_wdt.h`, `esp32-hal.h` and `Print.h`
- **ArduinoJson 7.x** — optional, only when `MYCILA_JSON_SUPPORT` is defined

## Supported Platforms

- **ESP32** (all variants: ESP32, ESP32-S2, ESP32-S3, ESP32-C3, ESP32-C6, ESP32-H2)
- **Arduino Framework** via PlatformIO or Arduino IDE

!!! note
The library targets the ESP32 Arduino core only. It is not a generic AVR/ARM scheduler because `asyncStart()`, the Watchdog and `esp32-hal.h` are ESP32-specific.

## Verify the Installation

```cpp
#include <MycilaTaskManager.h>

void setup() {
  Serial.begin(115200);
  Serial.printf("MycilaTaskManager %s\n", MYCILA_TASK_MANAGER_VERSION);
}

void loop() {}
```
