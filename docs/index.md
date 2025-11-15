# MycilaTaskManager

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![Continuous Integration](https://github.com/mathieucarbou/MycilaTaskManager/actions/workflows/ci.yml/badge.svg)](https://github.com/mathieucarbou/MycilaTaskManager/actions/workflows/ci.yml)
[![PlatformIO Registry](https://badges.registry.platformio.org/packages/mathieucarbou/library/MycilaTaskManager.svg)](https://registry.platformio.org/libraries/mathieucarbou/MycilaTaskManager)

Arduino / ESP32 Task Manager Library

This is a simple task manager for Arduino / ESP32 to schedule tasks at a given frequency.
Tasks are represented by anonymous functions, so they must be small, non-blocking and cooperative.

## Table of Contents

- [Features](#features)
- [Installation](#installation)
- [Usage](#usage)
  - [Simple Task](#simple-task)
  - [With a TaskManager](#with-a-taskmanager)
  - [Async Mode](#async-mode)
  - [Task Types](#task-types)
  - [Passing Data to Tasks](#passing-data-to-tasks)
  - [Conditional Task Execution](#conditional-task-execution)
  - [Profiling & Statistics](#profiling--statistics)
  - [JSON Support](#json-support)
  - [Watchdog Timer (WDT) Support](#watchdog-timer-wdt-support)
  - [Advanced Task Control](#advanced-task-control)
- [API Reference](#api-reference)
  - [Task Class](#task-class)
  - [TaskManager Class](#taskmanager-class)
  - [BinStatistics Class](#binstatistics-class)
- [Examples](#examples)
- [Building and Testing](#building-and-testing)
- [License](#license)
- [Contributing](#contributing)
- [Author](#author)
- [Version](#version)

## Features

- 🎯 **Dynamic Task Activation**: Enable/disable tasks dynamically with predicates
- 🔄 **Task Types**: Support for `ONCE` and `FOREVER` task types
- ⏰ **Flexible Scheduling**: Schedule tasks with intervals or run them immediately
- 📦 **Data Passing**: Pass custom data to tasks via `setData()`
- 🪝 **Callbacks**: Hook into task completion with `onDone()` callbacks
- 📊 **Profiling & Statistics**: Built-in execution time profiling with power-of-2 bin statistics
- ⏸️ **Pause & Resume**: Control task execution flow individually or for all tasks
- ⚡ **Early Run Requests**: Request immediate execution of scheduled tasks
- 🚀 **Async Support**: Run tasks in background FreeRTOS tasks with `.asyncStart()`
- 🐕 **Watchdog Timer (WDT) Support**: Protect against task hangs with ESP32 Task WDT
- 📄 **JSON Support**: Export task statistics and configuration to JSON (when `MYCILA_JSON_SUPPORT` is defined)

## Installation

### PlatformIO

```ini
lib_deps =
    mathieucarbou/MycilaTaskManager@^4.2.3
```

### Arduino IDE

Search for "MycilaTaskManager" in the Library Manager and install it.

## Usage

**Please have a look at the [examples](examples/) for complete usage patterns.**

### Simple Task

```cpp
#include <MycilaTaskManager.h>

Mycila::Task sayHello("sayHello", [](void* params) {
  Serial.println("Hello");
});

void setup() {
  Serial.begin(115200);

  sayHello.setEnabled(true);
  sayHello.setType(Mycila::Task::Type::FOREVER); // this is the default
  sayHello.setInterval(1000); // run every 1000ms
  sayHello.onDone([](const Mycila::Task& me, uint32_t elapsed) {
    ESP_LOGD("app", "Task '%s' executed in %" PRIu32 " us", me.name(), elapsed);
  });
}

void loop() {
  sayHello.tryRun();
}
```

### With a TaskManager

A `TaskManager` helps organize multiple tasks and execute them together:

```cpp
#include <MycilaTaskManager.h>

Mycila::TaskManager loopTaskManager("loop()");

Mycila::Task sayHello("sayHello", [](void* params) {
  Serial.println("Hello");
});

Mycila::Task sayGoodbye("sayGoodbye", [](void* params) {
  Serial.println("Goodbye");
});

void setup() {
  Serial.begin(115200);

  sayHello.setEnabled(true);
  sayHello.setType(Mycila::Task::Type::FOREVER);
  sayHello.setInterval(1000);
  sayHello.onDone([](const Mycila::Task& me, uint32_t elapsed) {
    sayGoodbye.resume(); // trigger the goodbye task
  });
  loopTaskManager.addTask(sayHello);

  sayGoodbye.setEnabled(true);
  sayGoodbye.setType(Mycila::Task::Type::ONCE); // runs once then pauses
  loopTaskManager.addTask(sayGoodbye);
}

void loop() {
  loopTaskManager.loop(); // executes all scheduled tasks
}
```

### Async Mode

Run a task manager in a background FreeRTOS task:

```cpp
void setup() {
  // ... configure tasks ...

  // Start async with custom parameters
  loopTaskManager.asyncStart(
    4096,  // stack size
    -1,    // priority (-1 = same as caller)
    -1,    // core ID (-1 = same as caller)
    10,    // delay in ms when no tasks execute
    false  // enable WDT for this task
  );
}

void loop() {
  // loop() is now free for other work, or can be deleted
  vTaskDelete(NULL); // delete loop task if not needed
}
```

See the [AsyncTaskManager example](examples/AsyncTaskManager) for a complete implementation.

### Task Types

- **`Mycila::Task::Type::FOREVER`**: Runs repeatedly at the specified interval (default)
- **`Mycila::Task::Type::ONCE`**: Runs once and automatically pauses itself

### Passing Data to Tasks

```cpp
char* message = "Pong";

Mycila::Task ping("ping", [](void* params) {
  Serial.println((const char*)params);
});

void setup() {
  ping.setData(message);
  ping.setEnabled(true);
  ping.setType(Mycila::Task::Type::ONCE);
}
```

### Conditional Task Execution

Use predicates to control task execution dynamically:

```cpp
bool systemReady = false;

Mycila::Task conditionalTask("conditional", [](void* params) {
  Serial.println("System is ready!");
});

void setup() {
  conditionalTask.setEnabledWhen([]() {
    return systemReady; // only runs when systemReady is true
  });
  conditionalTask.setInterval(1000);
}
```

### Profiling & Statistics

Enable profiling to track task execution times:

```cpp
void setup() {
  // Enable profiling for all tasks in the manager
  loopTaskManager.enableProfiling(
    6,  // taskManagerBinCount - bins for manager profiling
    10, // taskBinCount - bins for individual tasks
    1   // unitDividerMillis - 1 for milliseconds, 1000 for microseconds
  );

  // Or enable for individual tasks
  sayHello.enableProfiling(10, 1);
}

void loop() {
  loopTaskManager.loop();

  // Log statistics periodically
  static unsigned long lastLog = 0;
  if (millis() - lastLog > 5000) {
    loopTaskManager.log(); // prints statistics to serial
    lastLog = millis();
  }
}
```

The statistics use power-of-2 bins to track execution times. For example, with 10 bins:

- Bin 0: 0 ≤ elapsed < 2¹
- Bin 1: 2¹ ≤ elapsed < 2²
- Bin 2: 2² ≤ elapsed < 2³
- ...
- Bin 9: 2⁹ ≤ elapsed

### JSON Support

When compiled with `MYCILA_JSON_SUPPORT` defined, you can export task data to JSON:

```cpp
#include <ArduinoJson.h>
#include <MycilaTaskManager.h>

void outputJson() {
  JsonDocument doc;
  loopTaskManager.toJson(doc.to<JsonObject>());
  serializeJson(doc, Serial);
  Serial.println();
}
```

See the [TaskManagerJson example](examples/TaskManagerJson) for details.

### Watchdog Timer (WDT) Support

Protect against hung tasks using the ESP32 Task Watchdog Timer:

```cpp
void setup() {
  // Configure global WDT
  Mycila::TaskManager::configureWDT(5, false); // 5 seconds, no panic

  // Start async task manager with WDT enabled
  taskManager.asyncStart(4096, -1, -1, 10, true); // last param enables WDT
}
```

See the [Watchdog example](examples/Watchdog) for a complete demonstration.

### Advanced Task Control

```cpp
// Pause and resume individual tasks
myTask.pause();
myTask.resume();
myTask.resume(5000); // resume after 5 second delay

// Pause/resume all tasks in a manager
loopTaskManager.pause();
loopTaskManager.resume();

// Request early execution (skip waiting for interval)
myTask.requestEarlyRun();

// Force immediate execution
myTask.forceRun();

// Check task state
bool isRunning = myTask.running();
bool isPaused = myTask.paused();
bool isEnabled = myTask.enabled();
bool isScheduled = myTask.scheduled();
bool shouldRun = myTask.shouldRun();
uint32_t remaining = myTask.remainingTme(); // milliseconds until next run
```

## API Reference

### Task Class

#### Constructors

```cpp
Task(const char* name, Function fn)
Task(const char* name, Type type, Function fn)
```

#### Task Configuration Methods

- `Task& setType(Type type)` - Set task type (`ONCE` or `FOREVER`)
- `Task& setEnabled(bool enabled)` - Enable or disable the task
- `Task& setEnabledWhen(Predicate predicate)` - Set a predicate function for conditional execution
- `Task& setInterval(uint32_t intervalMillis)` - Set execution interval in milliseconds
- `Task& setData(void* params)` - Pass custom data to the task function
- `Task& onDone(DoneCallback callback)` - Set callback when task completes

#### Task Control Methods

- `Task& pause()` - Pause task execution
- `Task& resume(uint32_t delayMillis = 0)` - Resume task (optionally after delay)
- `Task& requestEarlyRun()` - Request immediate execution on next check
- `Task& forceRun()` - Execute task immediately
- `bool tryRun()` - Try to run task if conditions are met (returns true if executed)

#### Task State Query Methods

- `const char* name()` - Get task name
- `Type type()` - Get task type
- `bool enabled()` - Check if task is enabled
- `bool paused()` - Check if task is paused
- `bool running()` - Check if task is currently executing
- `bool scheduled()` - Check if task is scheduled (enabled and not paused)
- `bool shouldRun()` - Check if task should run now
- `bool earlyRunRequested()` - Check if early run was requested
- `uint32_t interval()` - Get interval in milliseconds
- `uint32_t remainingTme()` - Get remaining time until next run (milliseconds)
- `void* data()` - Get task data pointer

#### Task Profiling Methods

- `void enableProfiling(uint8_t binCount = 10, uint32_t unitDividerMillis = 1)` - Enable profiling
- `void disableProfiling()` - Disable profiling
- `bool profiled()` - Check if profiling is enabled
- `const BinStatistics* statistics()` - Get statistics object
- `Task& log()` - Log statistics to serial

#### Task JSON Methods

When `MYCILA_JSON_SUPPORT` is defined:

- `void toJson(const JsonObject& root)` - Export task state to JSON

### TaskManager Class

#### Constructor

```cpp
TaskManager(const char* name)
```

#### Task Management Methods

- `Task& newTask(const char* name, Task::Function fn)` - Create and add a new task
- `Task& newTask(const char* name, Task::Type type, Task::Function fn)` - Create and add a new task with type
- `void addTask(Task& task)` - Add an existing task reference
- `void removeTask(Task& task)` - Remove a task reference

#### Execution Methods

- `size_t loop()` - Execute all scheduled tasks (returns number executed)
- `bool asyncStart(uint32_t stackSize = 4096, BaseType_t priority = -1, BaseType_t coreID = -1, uint32_t delay = 10, bool wdt = false)` - Start async execution
- `void asyncStop()` - Stop async execution

#### TaskManager Control Methods

- `void pause()` - Pause all tasks
- `void resume(uint32_t delayMillis = 0)` - Resume all tasks
- `void setEnabled(bool enabled)` - Enable/disable all tasks

#### TaskManager Query Methods

- `const char* name()` - Get task manager name
- `size_t tasks()` - Get number of tasks
- `bool empty()` - Check if no tasks are registered

#### TaskManager Profiling Methods

- `void enableProfiling(uint8_t taskManagerBinCount, uint8_t taskBinCount, uint32_t unitDividerMillis = 1)` - Enable profiling for manager and all tasks
- `void enableProfiling(uint8_t taskManagerBinCount = 12, uint32_t unitDividerMillis = 1)` - Enable profiling for manager only
- `void disableProfiling()` - Disable profiling for manager and all tasks
- `void log()` - Log statistics for manager and all tasks

#### TaskManager JSON Methods

When `MYCILA_JSON_SUPPORT` is defined:

- `void toJson(const JsonObject& root)` - Export manager and all tasks to JSON

#### Static Methods

- `static bool configureWDT(uint32_t timeoutSeconds = CONFIG_ESP_TASK_WDT_TIMEOUT_S, bool panic = true)` - Configure global Task Watchdog Timer

### BinStatistics Class

Used internally for profiling. Tracks execution times in power-of-2 bins.

#### Methods

- `uint32_t unitDivider()` - Get unit divider
- `uint8_t bins()` - Get number of bins
- `uint32_t count()` - Get total number of recorded entries
- `uint16_t bin(uint8_t index)` - Get count for specific bin
- `void clear()` - Reset all statistics
- `void record(uint32_t elapsed)` - Record an execution time
- `void toJson(const JsonObject& root)` - Export to JSON (when `MYCILA_JSON_SUPPORT` is defined)

## Examples

The library includes several examples demonstrating different features:

- **[TaskManager](examples/TaskManager)** - Basic usage with multiple tasks, callbacks, and profiling
- **[AsyncTaskManager](examples/AsyncTaskManager)** - Running tasks in a background FreeRTOS task
- **[Watchdog](examples/Watchdog)** - Using the Task Watchdog Timer to protect against hung tasks
- **[TaskManagerJson](examples/TaskManagerJson)** - Exporting task statistics and configuration to JSON

## Building and Testing

This library is designed for ESP32 using Arduino framework. It uses FreeRTOS features and ESP32-specific APIs.

### Requirements

- ESP32 board (ESP-IDF v4.x or v5.x)
- Arduino framework
- ArduinoJson library (optional, for JSON support)

### Compilation Flags

- Define `MYCILA_JSON_SUPPORT` to enable JSON serialization features

## License

This library is licensed under the MIT License. See [LICENSE](LICENSE) file for details.

## Contributing

Contributions are welcome! Please feel free to submit a Pull Request.

## Author

Mathieu Carbou

- GitHub: [@mathieucarbou](https://github.com/mathieucarbou)

## Version

Current version: **4.2.3**
