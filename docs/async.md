# Async Execution

`asyncStart()` moves a `TaskManager` into its own FreeRTOS task, freeing `loop()` for other work — or letting you delete it entirely.

## Basic Async Manager

```cpp
#include <MycilaTaskManager.h>

Mycila::TaskManager tasks("bg");

void setup() {
  Serial.begin(115200);

  tasks.newTask("tick", [](void* params) {
    Serial.println("tick");
  }).setInterval(1000);

  tasks.asyncStart();
}

void loop() {
  // loop() is now free for other work
}
```

## Signature

```cpp
bool asyncStart(uint32_t stackSize = 4096,
                BaseType_t priority = -1,
                BaseType_t coreID = -1,
                uint32_t delay = 10,
                bool wdt = false);
```

| Parameter   | Default | Meaning                                                          |
| :---------- | ------: | :--------------------------------------------------------------- |
| `stackSize` |  `4096` | FreeRTOS stack size in bytes                                     |
| `priority`  |    `-1` | Task priority; `-1` inherits the caller's priority                |
| `coreID`    |    `-1` | Core affinity; `-1` pins to the core that called `asyncStart()`   |
| `delay`     |    `10` | Delay in ms applied at the end of each `loop()` pass             |
| `wdt`       | `false` | Add the task to the global Task Watchdog Timer                    |

Returns `true` when the FreeRTOS task was created. Calling `asyncStart()` twice returns `false` and does nothing.

!!! warning "Don't run `loop()` too"
Once started asynchronously, the background task calls `loop()` for you. Calling `tasks.loop()` from the Arduino `loop()` in parallel would execute the same tasks concurrently from two contexts.

## Stopping

```cpp
tasks.asyncStop();
```

Removes the task from the WDT (if registered), clears the internal run flag, and forgets the handle. The FreeRTOS task checks the flag after its current `loop()` pass and then calls `vTaskDelete(NULL)` to delete itself.

!!! warning "`asyncStop()` is asynchronous and racy right after `asyncStart()`"
`asyncStop()` only clears a flag — it does not join or delete the FreeRTOS task, so the task may still be inside its last `loop()` pass when `asyncStop()` returns.

Worse, the background task sets the flag to `true` as its **first** action. Calling `asyncStop()` immediately after `asyncStart()` (before the new task has been scheduled) can therefore lose the stop request entirely: the flag is cleared, then the task starts and sets it back to `true` — and the handle is already `NULL`, so it can no longer be stopped.

The practical consequence: **start an async manager once for the lifetime of the application** instead of starting and stopping it repeatedly. If you really need to suspend work, keep the manager running and pause the tasks:

```cpp
// preferred over asyncStop()/asyncStart() cycles
tasks.pause();   // stop executing tasks, keep the FreeRTOS task alive
tasks.resume();  // resume
```

## Deleting the Arduino Loop Task

If nothing else needs `loop()`, the canonical pattern is to delete it:

```cpp
void loop() {
  vTaskDelete(NULL);
}
```

This is what the [AsyncTaskManager example](examples.md) does.

## Watchdog Timer

### Configure the global TWDT

```cpp
Mycila::TaskManager::configureWDT(5, false); // 5 s timeout, no panic
```

| Parameter        | Default                              | Meaning                          |
| :--------------- | :----------------------------------- | :------------------------------- |
| `timeoutSeconds` | `CONFIG_ESP_TASK_WDT_TIMEOUT_S`      | Watchdog timeout                 |
| `panic`          | `true`                               | Panic (reboot) instead of logging |

The function handles both ESP-IDF v4 (`esp_task_wdt_init(timeout, panic)`) and v5+ (`esp_task_wdt_config_t` + `esp_task_wdt_reconfigure()`), so it is safe to call on an already-initialised WDT.

### Register an async manager

```cpp
Mycila::TaskManager::configureWDT(5, false);
tasks.asyncStart(4096, -1, -1, 10, true); // last argument registers the TWDT
```

While registered, the background task calls `esp_task_wdt_reset()` after every `loop()` pass. If a task blocks for longer than the timeout, the WDT fires.

The background task is a simple loop:

```cpp
while (_allowedToRun) {
  loop(_delay);
  if (_wdt)
    esp_task_wdt_reset();
}
vTaskDelete(NULL);
```

!!! tip
The Watchdog example deliberately blocks tasks for up to 6 seconds with a 5-second timeout to demonstrate the feature — see [Examples](examples.md).

## Choosing Between Sync and Async

| Scenario                                              | Recommendation        |
| :---------------------------------------------------- | :-------------------- |
| A few tasks, plenty of headroom in `loop()`            | Synchronous `loop()`  |
| `loop()` is already used by another framework         | Async                 |
| Tasks must not be jittered by unrelated `loop()` work  | Async, own core       |
| You need WDT supervision of your task code            | Async + `wdt = true`  |

## Caveats

- **Core affinity** — leaving `coreID` at `-1` binds the task to the caller's core. On a dual-core ESP32, pass `0` or `1` explicitly if you want a specific core.
- **Priority** — `-1` inherits the caller's priority (`uxTaskPriorityGet(NULL)`). Raising it above the idle task is fine; raising it above `loopTask` will starve `loop()`.
- **Shared state** — tasks running in different managers (or in sync + async mode) still need your own synchronisation around shared data. The library does not add locking.
