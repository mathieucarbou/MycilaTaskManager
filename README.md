# MycilaTaskManager

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![Continuous Integration](https://github.com/mathieucarbou/MycilaTaskManager/actions/workflows/ci.yml/badge.svg)](https://github.com/mathieucarbou/MycilaTaskManager/actions/workflows/ci.yml)
[![PlatformIO Registry](https://badges.registry.platformio.org/packages/mathieucarbou/library/MycilaTaskManager.svg)](https://registry.platformio.org/libraries/mathieucarbou/MycilaTaskManager)

Arduino / ESP32 Task Manager Library — runs cooperative, non-blocking tasks at a chosen frequency, on the `loop()` or in a dedicated FreeRTOS task.

> **[📖 Full documentation and API reference → mathieu.carbou.me/MycilaTaskManager](https://mathieu.carbou.me/MycilaTaskManager/)**

## Setup

### PlatformIO

```ini
[env:myproject]
platform = espressif32
framework = arduino
board = esp32dev

lib_deps =
  mathieucarbou/MycilaTaskManager@^4.2.5
```

### Arduino IDE

1. Go to **Sketch** → **Include Library** → **Manage Libraries**
2. Search for `MycilaTaskManager`
3. Install the library by Mathieu Carbou

### Minimal Sketch

```cpp
#include <MycilaTaskManager.h>

Mycila::TaskManager tasks("loop()");

void setup() {
  Serial.begin(115200);

  tasks.newTask("hello", [](void* params) {
    Serial.println("Hello");
  }).setInterval(1000);
}

void loop() {
  tasks.loop();
}
```

### Requirements

- **ESP32** (any variant) with the Arduino framework
- **[ArduinoJson](https://github.com/bblanchon/ArduinoJson) 7.x** — only when `MYCILA_JSON_SUPPORT` is defined

## Configuration Macros

All configuration is compile-time. Set the flags in `build_flags`:

```ini
build_flags =
  -D MYCILA_JSON_SUPPORT
```

### Feature Flags

| Macro                             | Effect                                                                                                  |
| :-------------------------------- | :------------------------------------------------------------------------------------------------------ |
| `MYCILA_JSON_SUPPORT`             | Enables `toJson()` on `Task`, `TaskManager` and `BinStatistics`. Requires ArduinoJson 7.x.               |
| `MYCILA_TASK_MANAGER_NO_PARAMS`   | Task functions become `std::function<void()>`. Removes `setData()` / `data()` and the `void* params` argument. |
| `MYCILA_TASK_MANAGER_NO_CALLBACKS`| Removes `onDone()` and the done callback.                                                               |
| `MYCILA_TASK_MANAGER_NO_STATS`    | Removes all profiling: `enableProfiling()`, `statistics()`, `BinStatistics`. `profiled()` returns `false`. |

> **⚠️ `MYCILA_TASK_MANAGER_NO_PARAMS` changes the task lambda signature**
>
> ```cpp
> // default
> Mycila::Task t("t", [](void* params) { /* ... */ });
>
> // with MYCILA_TASK_MANAGER_NO_PARAMS
> Mycila::Task t("t", []() { /* ... */ });
> ```

### Version Macros

Always defined, for runtime or compile-time checks:

```cpp
MYCILA_TASK_MANAGER_VERSION           // "4.2.5"
MYCILA_TASK_MANAGER_VERSION_MAJOR     // 4
MYCILA_TASK_MANAGER_VERSION_MINOR     // 2
MYCILA_TASK_MANAGER_VERSION_REVISION  // 5
```

### Logging

Logs use the `TASKS` tag through `ESP_LOGx`. Control the verbosity with the Arduino core macro:

```ini
build_flags =
  -D CORE_DEBUG_LEVEL=ARDUHAL_LOG_LEVEL_DEBUG
```

## License

This project is licensed under the MIT License — see the [LICENSE](LICENSE) file for details.

---

**Author**: [Mathieu Carbou](https://github.com/mathieucarbou)
