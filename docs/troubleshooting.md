# Troubleshooting

## Common Issues

### A task never runs

- **`ONCE` tasks start paused** — call `resume()` (or `resume(ms)`) to schedule the single run.
- **Interval not elapsed** — check `remainingTime()`; an interval of `1000` means at most one run per second.
- **Enabled predicate false** — `enabled()` reports the predicate result. If you used `setEnabledWhen()`, make sure the captured variable is actually updated.
- **Task never added to a manager** — `addTask()` / `newTask()` are mandatory when driving through `loop()`.

### A task runs too often

- The default `TaskManager::loop(delayMillis = 10)` throttles to ~100 Hz, but a task with `interval == 0` runs on every pass.
- `forceRun()` bypasses the interval entirely — make sure it is not called from a tight loop.

### `loop()` feels sluggish

`TaskManager::loop()` adds a **10 ms delay per pass** by default. Pass an explicit smaller value:

```cpp
tasks.loop(1);
```

### The other tasks starve

The scheduler is cooperative: a blocking task blocks everything else **in the same manager**. Look for `delay()`, `while (!Serial)`, network calls or long file I/O inside task functions. If a task must block, give it its own manager (and possibly its own FreeRTOS task).

### Watchdog resets / "task_wdt: Task watchdog got triggered"

- A task blocked longer than the configured TWDT timeout. The [Watchdog example](examples.md) demonstrates this deliberately.
- Increase the timeout with `Mycila::TaskManager::configureWDT(seconds, panic)`.
- Only the **async** manager resets the WDT. Synchronous managers are not supervised by this library.

### Tasks run twice

You started the manager asynchronously **and** still call `tasks.loop()` from the Arduino `loop()`. Remove one of the two:

```cpp
void loop() {
  vTaskDelete(NULL); // async mode: the background task drives loop()
}
```

### `asyncStart()` returns false

The manager is already running. Call `asyncStop()` before starting it again.

### Statistics are always empty

- Profiling was not enabled, or was enabled **before** the tasks were added (`enableProfiling()` only walks the current task list).
- `MYCILA_TASK_MANAGER_NO_STATS` is defined.
- The task has simply never run — `log()` only prints entries with a non-zero count.

### A `ONCE` task keeps running after being switched back

`setType()` returns early when the type is unchanged, so it will not re-pause a task that you already resumed:

```cpp
task.resume();                          // un-paused
task.setType(Mycila::Task::Type::ONCE); // no-op: identical type, stays un-paused
```

Call `resume()` — not `setType()` — to (re-)arm a `ONCE` task.

### `enableProfiling(6, 10)` does not compile

*"call to member function 'enableProfiling' is ambiguous"* — the two `TaskManager` overloads overlap for two arguments. Pass one or three:

```cpp
tasks.enableProfiling(6);         // manager only
tasks.enableProfiling(6, 10, 1);  // manager + every registered task
```

### `remainingTime()` says the task is waiting, but it runs anyway

A never-run task has `lastEndTime() == 0`, which `shouldRun()` treats as *due now*, while `remainingTime()` still computes `interval - millis()`. Use `shouldRun()` to decide whether a task will run, not `remainingTime()`.

### `resume(ms)` changed the task period

`resume(delayMillis)` calls `setInterval(delayMillis)`, so the configured interval is permanently replaced. A later bare `resume()` does not restore it — call `setInterval()` again explicitly.

### Crash when a profiled `Task` is copied

`Task`'s implicit copy constructor duplicates the `BinStatistics*`, and both destructors `delete` it. Declare tasks at file scope or as members; do not copy a task that has profiling enabled.

### `enableProfiling()` seems ignored

Statistics are created only once. Calling `enableProfiling()` a second time with a different bin count or divider does nothing. Call `disableProfiling()` first:

```cpp
tasks.disableProfiling();
tasks.enableProfiling(10, 6, 1);
```

### An async manager cannot be stopped

`asyncStop()` only clears a flag, and the background task sets that flag to `true` when it starts. Stopping immediately after `asyncStart()` can lose the request, leaving a task that can no longer be stopped because the handle was already cleared. Start the async manager once and use `pause()` / `resume()` to control it — see [Async Execution](async.md).

### `b = a;` does not compile on a `Task`

`Task` has a `const std::function` member, which deletes its copy assignment operator. The copy constructor still works. Declare tasks at file scope or as members instead of assigning them.

### `statistics()` / `profiled()` do not compile

`statistics()` does not exist when `MYCILA_TASK_MANAGER_NO_STATS` is defined. `profiled()` still compiles and returns `false`.

### `onDone was not declared`

`MYCILA_TASK_MANAGER_NO_CALLBACKS` is defined. Remove the flag or drop the callback.

### `setData was not declared`

`MYCILA_TASK_MANAGER_NO_PARAMS` is defined. In that mode task functions take no argument and must be declared as `[]() { ... }`.

### `toJson` not found

Add `-D MYCILA_JSON_SUPPORT` and install ArduinoJson. See [JSON Output](json.md).

### Build fails on a non-ESP32 board

The library includes `<esp_task_wdt.h>`, `<esp32-hal.h>` and FreeRTOS headers. Only the ESP32 Arduino core is supported.

## Debugging Tips

- Log the whole manager periodically: `tasks.log()`.
- Log a single task: `task.log()`.
- Dump the state as JSON to inspect pause/enable/interval: see [JSON Output](json.md).
- Use `remainingTime()` while debugging a scheduling surprise.
