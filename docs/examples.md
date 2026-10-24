# Examples

The library ships four examples covering the main usage patterns.

## Available Examples

| Example                                                                                                | Description                                              |
| :----------------------------------------------------------------------------------------------------- | :------------------------------------------------------- |
| [TaskManager](https://github.com/mathieucarbou/MycilaTaskManager/tree/main/examples/TaskManager/)       | Synchronous manager driven from `loop()`, with statistics |
| [AsyncTaskManager](https://github.com/mathieucarbou/MycilaTaskManager/tree/main/examples/AsyncTaskManager/) | Manager running in its own FreeRTOS task             |
| [Watchdog](https://github.com/mathieucarbou/MycilaTaskManager/tree/main/examples/Watchdog/)             | One sync and one async manager, the async one supervised by the TWDT |
| [Json](https://github.com/mathieucarbou/MycilaTaskManager/tree/main/examples/Json/)                     | JSON export of the task tree alongside classic logging    |

## TaskManager

Shows the core patterns: two periodic tasks, an `ONCE` task re-armed from a callback, a delayed `ONCE` task, and periodic statistics logging.

```cpp
Mycila::TaskManager loopTaskManager("loop()");

Mycila::Task sayHello("sayHello", [](void* params) { Serial.println("Hello"); delay(random(1, 500)); });
Mycila::Task sayGoodbye("sayGoodbye", [](void* params) { Serial.println("Hello"); delay(random(1, 500)); });
Mycila::Task ping("ping", [](void* params) { Serial.println((const char*)params); delay(random(1, 500)); });
Mycila::Task delayed("delayed", [](void* params) { Serial.println("Delayed!"); delay(random(1, 500)); });
Mycila::Task output("output", [](void* params) { loopTaskManager.log(); });

char* params = "Pong";

void setup() {
  Serial.begin(115200);
  while (!Serial)
    continue;

  sayHello.setType(Mycila::Task::Type::FOREVER);
  sayHello.setInterval(1000);
  sayHello.onDone([](const Mycila::Task& me, uint32_t elapsed) {
    ESP_LOGD("app", "Task '%s' executed in %" PRIu32 " us", me.name(), elapsed);
  });
  sayHello.setEnabled(true);
  loopTaskManager.addTask(sayHello);

  sayGoodbye.setType(Mycila::Task::Type::FOREVER);
  sayGoodbye.setInterval(3000);
  sayGoodbye.onDone([](const Mycila::Task& me, uint32_t elapsed) {
    ESP_LOGD("app", "Task '%s' executed in %" PRIu32 " us", me.name(), elapsed);
    ping.setData(params);
    ping.resume(); // re-arm the ONCE task
  });
  sayGoodbye.setEnabled(true);
  loopTaskManager.addTask(sayGoodbye);

  ping.setType(Mycila::Task::Type::ONCE);
  ping.onDone([](const Mycila::Task& me, uint32_t elapsed) {
    ESP_LOGD("app", "Task '%s' executed in %" PRIu32 " us", me.name(), elapsed);
  });
  ping.setEnabled(true);
  loopTaskManager.addTask(ping);

  output.setType(Mycila::Task::Type::FOREVER);
  output.setInterval(5000);
  output.setEnabled(true);
  loopTaskManager.addTask(output);

  delayed.setType(Mycila::Task::Type::ONCE);
  delayed.setEnabled(true);
  loopTaskManager.addTask(delayed);

  loopTaskManager.enableProfiling(6);

  delayed.resume(10000); // run once, 10 seconds from now
}

void loop() {
  loopTaskManager.loop();
}
```

Key takeaways:

- `ping` is a `ONCE` task, initially paused; `sayGoodbye`'s `onDone()` re-arms it with a payload every 3 s.
- `delayed.resume(10000)` is the idiomatic "run in 10 seconds" pattern.
- `enableProfiling(6)` must be called after the tasks are added.
- The `delay(random(1, 500))` calls simulate workloads — and illustrate the cooperative limitation: while one runs, the others wait.

## AsyncTaskManager

Identical task graph, but the manager runs in a background FreeRTOS task and the Arduino loop task deletes itself:

```cpp
void setup() {
  // ... same task configuration ...

  loopTaskManager.enableProfiling(6);
  loopTaskManager.asyncStart();
}

void loop() {
  vTaskDelete(NULL);
}
```

See [Async Execution](async.md) for the `asyncStart()` parameters.

## Watchdog

Two managers: `tm-1` runs asynchronously and is registered with the Task Watchdog, `tm-2` runs synchronously from `loop()`. Each has a task that blocks for a random 1–6 seconds — long enough to trip a 5-second WDT:

```cpp
Mycila::TaskManager taskManager1("tm-1");
Mycila::TaskManager taskManager2("tm-2");

Mycila::Task slowdown1("slowdown1", [](void* params) {
  long d = random(1000, 6000);
  Serial.printf("[slowdown1] Delaying for %ld ms\n", d);
  delay(d);
});

Mycila::Task slowdown2("slowdown2", [](void* params) {
  long d = random(1000, 6000);
  Serial.printf("[slowdown2] Delaying for %ld ms\n", d);
  delay(d);
});

void setup() {
  Serial.begin(115200);
  while (!Serial)
    continue;

  slowdown1.setType(Mycila::Task::Type::FOREVER);
  taskManager1.addTask(slowdown1);

  slowdown2.setType(Mycila::Task::Type::FOREVER);
  taskManager2.addTask(slowdown2);

  Mycila::TaskManager::configureWDT(5, false);

  taskManager1.asyncStart(4096, -1, -1, 10, true); // register with the WDT
}

void loop() {
  taskManager2.loop();
}
```

## Json

Same task graph as `TaskManager`, but the `output` task serializes the whole manager:

```cpp
Mycila::Task output("output", [](void* params) {
  JsonDocument doc;
  loopTaskManager.toJson(doc.to<JsonObject>());
  serializeJson(doc, Serial);
  Serial.println();
  loopTaskManager.log();
});
```

Requires `-D MYCILA_JSON_SUPPORT` and ArduinoJson. See [JSON Output](json.md).

## Building an Example

The repository is set up as a self-contained PlatformIO project: `lib_dir = .` compiles the library from the root, and `PLATFORMIO_SRC_DIR` selects the example. `platformio.ini` also ships commented `src_dir` lines for the same purpose.

### PlatformIO

```bash
# regular examples
PLATFORMIO_SRC_DIR=examples/TaskManager      pio run -e arduino-3
PLATFORMIO_SRC_DIR=examples/AsyncTaskManager pio run -e arduino-3
PLATFORMIO_SRC_DIR=examples/Watchdog         pio run -e arduino-3

# JSON example (defines MYCILA_JSON_SUPPORT and pulls ArduinoJson)
PLATFORMIO_SRC_DIR=examples/Json             pio run -e json
```

Alternatively, edit `platformio.ini` directly:

```ini
[platformio]
lib_dir = .
default_envs = arduino-3
src_dir = examples/TaskManager
```

The CI uses dedicated environments that take the board from the `PIO_BOARD` environment variable:

```bash
PLATFORMIO_SRC_DIR=examples/TaskManager PIO_BOARD=esp32dev pio run -e ci-arduino-3
PLATFORMIO_SRC_DIR=examples/Json        PIO_BOARD=esp32dev pio run -e ci-json
```

### Arduino IDE

Open the `.ino` file of the example you want (for instance `examples/TaskManager/TaskManager.ino`). The sketch includes `<MycilaTaskManager.h>`, so the library must be installed through the Library Manager.

The `Json` example additionally needs ArduinoJson **and** the `MYCILA_JSON_SUPPORT` symbol. The Arduino IDE has no per-sketch preprocessor definition UI, so adding `#define MYCILA_JSON_SUPPORT` at the very top of the sketch — before the includes — is the simplest way to build it there. PlatformIO users get it automatically from the `json` environment.
