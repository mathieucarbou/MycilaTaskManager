# Configuration

The library is configured entirely through preprocessor flags — there is no runtime configuration object.

## Build Flags

### JSON Support

Enables `toJson()` on tasks, managers and statistics. Requires ArduinoJson.

```ini
build_flags =
  -D MYCILA_JSON_SUPPORT

lib_deps =
  bblanchon/ArduinoJson
```

See [JSON Output](json.md).

### Feature Removal

Three flags strip optional features at compile time. They are meant for size-constrained builds.

| Flag                              | Removes                                                       |
| :-------------------------------- | :------------------------------------------------------------ |
| `MYCILA_TASK_MANAGER_NO_PARAMS`   | The `void* params` argument of task functions (`setData()`, `data()`) |
| `MYCILA_TASK_MANAGER_NO_CALLBACKS`| `onDone()` and the done callback                              |
| `MYCILA_TASK_MANAGER_NO_STATS`    | All profiling: `enableProfiling()`, `statistics()`, `BinStatistics` |

```ini
build_flags =
  -D MYCILA_TASK_MANAGER_NO_PARAMS
  -D MYCILA_TASK_MANAGER_NO_CALLBACKS
  -D MYCILA_TASK_MANAGER_NO_STATS
```

!!! warning "Signature changes with `NO_PARAMS`"
With `MYCILA_TASK_MANAGER_NO_PARAMS`, `Task::Function` becomes `std::function<void()>`. Every task lambda must then take **no** arguments:

```cpp
// With params (default)
Mycila::Task t("t", [](void* params) { /* ... */ });

// With MYCILA_TASK_MANAGER_NO_PARAMS
Mycila::Task t("t", []() { /* ... */ });
```

`setData()`/`data()` do not exist in that configuration.

## Version Macros

Available at compile time:

```cpp
MYCILA_TASK_MANAGER_VERSION           // "4.2.5"
MYCILA_TASK_MANAGER_VERSION_MAJOR     // 4
MYCILA_TASK_MANAGER_VERSION_MINOR     // 2
MYCILA_TASK_MANAGER_VERSION_REVISION  // 5
```

## PlatformIO Development Setup

A full development configuration for working on the library itself:

```ini
[platformio]
lib_dir = .
default_envs = arduino-3
src_dir = examples/TaskManager   ; or AsyncTaskManager, Json, Watchdog

[env]
platform = https://github.com/pioarduino/platform-espressif32/releases/download/55.03.311/platform-espressif32.zip
framework = arduino
board = esp32dev
build_flags =
  -std=c++17
  -std=gnu++17
  -Wall -Wextra
  -D CONFIG_ARDUHAL_LOG_COLORS
  -D CORE_DEBUG_LEVEL=ARDUHAL_LOG_LEVEL_DEBUG
build_unflags =
  -std=gnu++11
monitor_speed = 115200
monitor_filters = esp32_exception_decoder, log2file

[env:json]
build_flags =
  ${env.build_flags}
  -D MYCILA_JSON_SUPPORT
lib_deps =
  bblanchon/ArduinoJson @ 7.4.3
```

`lib_dir = .` makes PlatformIO compile the library from the repository root, which is what allows switching `src_dir` between examples.

## Logging

The library logs through `ESP_LOGx` with the `TASKS` tag:

```cpp
- tag: TASKS
```

Set the log level globally:

```ini
build_flags =
  -D CORE_DEBUG_LEVEL=ARDUHAL_LOG_LEVEL_DEBUG
```

At `DEBUG` level, `asyncStart()` / `asyncStop()` and WDT registration also produce messages. `TaskManager::log()` and `Task::log()` always use `ESP_LOGI`.

## C++ Standard

The library uses `std::function`, `std::list`, `std::shared_ptr` and `std::move`, all available since **C++11**. The shipped `platformio.ini` and CI build with **C++17** (`-std=c++17 -std=gnu++17`, with `-std=gnu++11` unset), which is the recommended configuration.

## Troubleshooting Build Issues

### `esp_task_wdt.h: No such file or directory`

You are building for a non-ESP32 target. The library only supports the ESP32 Arduino core.

### `std::function` conversion errors on task lambdas

Either the lambda signature does not match `Task::Function` (`void(void*)`) or you enabled `MYCILA_TASK_MANAGER_NO_PARAMS` while still declaring `void* params`.

### `onDone` / `setData` / `enableProfiling` not found

One of the `NO_*` flags is defined. Check `build_flags` — including flags inherited through `${env.build_flags}`.
