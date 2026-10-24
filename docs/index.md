# MycilaTaskManager

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![Continuous Integration](https://github.com/mathieucarbou/MycilaTaskManager/actions/workflows/ci.yml/badge.svg)](https://github.com/mathieucarbou/MycilaTaskManager/actions/workflows/ci.yml)
[![PlatformIO Registry](https://badges.registry.platformio.org/packages/mathieucarbou/library/MycilaTaskManager.svg)](https://registry.platformio.org/libraries/mathieucarbou/MycilaTaskManager)

A small, dependency-free task scheduler for Arduino / ESP32 that runs cooperative, non-blocking tasks at a chosen frequency — on the `loop()` or in a dedicated FreeRTOS task.

## Overview

Tasks are ordinary anonymous functions wrapped in a `Mycila::Task`. You decide **when** they are allowed to run (`setEnabled()`, `setEnabledWhen()`), **how often** (`setInterval()`), and **how many times** (`Type::ONCE` or `Type::FOREVER`). A `Mycila::TaskManager` groups tasks together so they can be started, paused, profiled and serialized in one call.

**Key Benefits:**

- **No magic, no threads required** — everything runs from your `loop()`, or from an optional background FreeRTOS task
- **Predictable scheduling** — millisecond interval per task, with immediate, early and delayed runs
- **Observable** — power-of-2 execution-time histograms for the manager and for each task
- **Safe** — optional ESP32 Task Watchdog Timer integration to catch hung tasks
- **Introspectable** — optional ArduinoJson export of the whole task tree
- **Tiny** — a single header, `std::function` based, no dynamic timers or ISRs

## Features

- 🎯 **Dynamic Task Activation**: Enable/disable tasks at runtime, or bind them to a predicate
- 🔄 **Task Types**: `ONCE` (runs and pauses itself) and `FOREVER` (runs every interval)
- ⏰ **Flexible Scheduling**: Interval based, immediate (`forceRun()`), delayed (`resume(ms)`) or early (`requestEarlyRun()`)
- 📦 **Data Passing**: Hand a `void*` payload to a task with `setData()`
- 🪝 **Callbacks**: Observe completion and elapsed time with `onDone()`
- 📊 **Profiling & Statistics**: Power-of-2 bin histograms, per task and per manager
- ⏸️ **Pause & Resume**: Suspend one task, or every task of a manager
- 🚀 **Async Support**: Run a manager in the background with `asyncStart()`
- 🐕 **Watchdog Support**: Tie an async manager to the ESP32 Task WDT
- 📄 **JSON Support**: Export tasks, scheduling state and statistics with `MYCILA_JSON_SUPPORT`

## Task Types

| Type                          | Behaviour                                               |
| :---------------------------- | :------------------------------------------------------ |
| `Mycila::Task::Type::FOREVER` | Runs at the configured interval, forever, while enabled |
| `Mycila::Task::Type::ONCE`    | Runs once, then pauses itself automatically             |

!!! note "`ONCE` tasks start paused"
Assigning `Type::ONCE` pauses the task. Call `resume()` (optionally with a delay) to let it run once.

## Quick Start

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
  tasks.loop(); // runs every task whose interval has elapsed
}
```

## Next Steps

- [Installation](installation.md) — add the library to your project
- [Tasks](tasks.md) — the building block: types, intervals, state and control
- [Task Managers](task-managers.md) — grouping and driving tasks
- [Async Execution](async.md) — run a manager in a background FreeRTOS task
- [Profiling & Statistics](statistics.md) — understand where time is spent
- [API Reference](api.md) and the generated [C++ API](cppapi/annotated.md)
- [Migration](migration.md) — coming from v4? Read this first: `pause()`, `resume()` and `waitForAllTasksToComplete()` changed behaviour

## License

This project is licensed under the MIT License — see the [LICENSE](https://github.com/mathieucarbou/MycilaTaskManager/blob/main/LICENSE) file for details.

## Disclaimer

_This website is provided for informational purposes only. By accessing this site and using the information contained herein, you accept the terms set forth in this disclaimer._

- _**Accuracy of Information**: We strive to provide accurate and up-to-date information on this site, but we cannot guarantee the completeness or accuracy of this information. The information provided is subject to change without notice._

- _**Use of Information**: Use of the information provided on this site is at your own risk. We decline all responsibility for the consequences arising from the use of this information. It is recommended that you consult a competent professional for advice specific to your situation._

- _**External Links**: This site may contain links to external websites which are provided for your reference and convenience. We have no control over the content of these websites and accept no responsibility for them. To the extent permitted by applicable law, we disclaim all liability for any direct, indirect, incidental, consequential or special damages arising out of the use of, or inability to use, this website. If you do not agree to these terms, please do not use this site._

---

**Author**: [Mathieu Carbou](https://github.com/mathieucarbou)
