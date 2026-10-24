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

The practical consequence: **start an async manager once for the lifetime of the application** instead of starting and stopping it repeatedly. If you really need to suspend work, keep the manager running and pause it:

```cpp
// preferred over asyncStop()/asyncStart() cycles
tasks.pause();   // stop dispatching tasks, keep the FreeRTOS task alive
tasks.resume();  // resume
```

## Pausing the Manager

`pause()` stops **dispatching**, not the tasks. It is the execution boundary you need before an OTA update, a reset, or any operation that requires exclusive access to a shared resource (LittleFS, a bus, a peripheral):

```cpp
tasks.pause();                 // request, non-blocking, does not touch any task
tasks.waitForPaused(5000);     // wait for the confirmation, false on timeout
// here: no callback of this manager is active, and none will start
// ... do the protected work ...
tasks.resume();                // continue, from where the tasks were
```

### The three states

```cpp
enum class State : uint8_t { RUNNING, PAUSING, PAUSED };
```

```
   pause()              acknowledged at an execution boundary
RUNNING ─────────▶ PAUSING ────────────────────────────────────────────▶ PAUSED
   ▲                                                                       │
   └────────────────────────── resume() ────────────────────────────┘
```

| State     | Meaning                                                                                |
| :-------- | :------------------------------------------------------------------------------------- |
| `RUNNING` | dispatching tasks, or ready to                                                          |
| `PAUSING` | a pause was requested, an execution (task code, statistics and `onDone`) is still in flight |
| `PAUSED`  | confirmed: no callback is active and none will start until `resume()`            |

A **request** guarantees nothing. Only a **confirmed** pause does. Check with `state()`, `paused()` (i.e. `state() == PAUSED`) or `waitForPaused(timeoutMs)`.

### What a pause does not do

- It does not change any per-task state: pending tasks stay pending, completed `ONCE` tasks stay paused, intervals, predicates and timestamps are untouched. On `resume()`, time having passed, overdue tasks simply run.
- It does not let a task bypass the gate: a callback that calls `someTask.resume()` only changes that task's state, the manager still refuses to dispatch it.
- It does not cover work outside this manager: other managers, timers, HTTP/BLE callbacks and explicit `forceRun()` calls from another context still run.

The task being executed finishes normally, `onDone` included — the gate is only checked **between** two executions. `loop()` and `Task::tryRun()` remain synchronous.

### Waiting for the confirmation

`waitForPaused()` polls `state()` every millisecond and returns `false` on timeout, so you keep control of the failure path:

```cpp
tasks.pause();
if (!tasks.waitForPaused(5000)) {
  tasks.resume();             // cancel the request
  return;                            // do NOT start the protected operation
}
```

`pause()`, `resume()` and `waitForPaused()` can be called from any FreeRTOS task. `waitForPaused()` returns immediately (without waiting) when called from a task of the manager itself, or from inside one of its callbacks: a pause requested from within a callback can only be confirmed once that callback returns, so waiting there would never succeed.

Requesting the pause **from a callback** is supported and useful (a task can ask the manager to stop after it finishes). *Waiting* for it from there is not: `state()` stays `PAUSING` until your callback returns.

!!! warning "Never wait for a pause from a callback"
### Watchdog and stop behaviour

While `PAUSED`, the async worker keeps running and keeps calling `esp_task_wdt_reset()`, so a long deliberate pause does not trip the TWDT. `asyncStop()` also works while paused: it lifts the pause and wakes the worker so it reaches its exit check. The pause state is cleared too, so a later `asyncStart()` on the same manager always restarts in `RUNNING`, never in the paused state it was left in.

`resume()` and `asyncStop()` wake the worker through a FreeRTOS semaphore, so neither has to wait for the configured `delay` (10 ms by default).

A paused manager does not dispatch: `loop()` returns `0`. It still honours its `delayMillis` pacing argument, so a synchronous caller cannot turn into a busy loop while the manager is paused — see [Task Managers](task-managers.md).

### Pausing the manager versus pausing the tasks

| Call                         | Effect                                                                      |
| :--------------------------- | :-------------------------------------------------------------------------- |
| `pause()`                    | stop **dispatching**: the manager gate                                       |
| `resume()`                   | reopen the gate, leaving every task state untouched                          |
| `pauseTasks()`               | call `Task::pause()` on every task; the manager keeps dispatching             |
| `resumeTasks(delayMillis=0)` | call `Task::resume()` on every task, **including** completed or intentionally paused ones |

In v4, `TaskManager::pause()` was implemented by pausing every task while the manager kept dispatching; that per-task behaviour is now `pauseTasks()` / `resumeTasks()`, and `pause()` really pauses the manager. See the migration note in [Task Managers](task-managers.md).


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
