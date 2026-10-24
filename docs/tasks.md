# Tasks

A `Mycila::Task` is the unit of work. It holds your function, its scheduling state (type, interval, pause/enable), an optional data payload and an optional completion callback.

## Creating a Task

```cpp
#include <MycilaTaskManager.h>

// Default type is FOREVER
Mycila::Task sayHello("sayHello", [](void* params) {
  Serial.println("Hello");
});

// Explicit type
Mycila::Task boot("boot", Mycila::Task::Type::ONCE, [](void* params) {
  Serial.println("Booting");
});
```

The constructor signature is `Task(const char* name, Function fn)` or `Task(const char* name, Type type, Function fn)`. The name must be a stable string literal or a `static` buffer: the task keeps the raw pointer.

!!! warning "Tasks are cooperative"
A task must be **short and non-blocking**. The scheduler never preempts a running task: while your function runs, no other task of the same manager is executed. Long `delay()` calls starve the other tasks (see the [Watchdog example](examples.md) which demonstrates exactly this failure).

## Task Types

### FOREVER

The task runs every time its interval elapses, for as long as it is enabled and not paused. This is the default.

```cpp
Mycila::Task poll("poll", [](void* params) {
  // read a sensor
});
poll.setType(Mycila::Task::Type::FOREVER);
poll.setInterval(1000); // every second
```

### ONCE

The task runs at most one time, then pauses itself. A `ONCE` task is **created paused**, so it will not run until you call `resume()`.

```cpp
Mycila::Task boot("boot", Mycila::Task::Type::ONCE, [](void* params) {
  Serial.println("Boot sequence done");
});
boot.setEnabled(true);
boot.resume();       // run on the next loop()
// or: boot.resume(5000); // run 5 seconds from now
```

This makes `ONCE` tasks a convenient deferred-call primitive: re-arm one at any time with `resume()`.

!!! note "`setType()` only pauses on an actual type change"
`setType()` returns early when the type is unchanged, so it will **not** re-pause a task:

```cpp
boot.resume();                        // un-paused, run once
boot.setType(Mycila::Task::Type::ONCE); // no-op: already ONCE, stays un-paused
```

To re-arm, call `resume()` again. Switching a finished `ONCE` task to `FOREVER` un-pauses it immediately, since the type actually changed.

!!! note "`lastEndTime() == 0` means “due now”"
A task with no interval, or one that has never run, has `lastEndTime() == 0` and is considered due immediately. This is also why `requestEarlyRun()` works by resetting that value.

## Scheduling

### Interval

`setInterval(uint32_t intervalMillis)` defines the minimum time between the end of one run and the beginning of the next.

```cpp
task.setInterval(250); // 4 times per second at most
```

An interval of `0` means the task runs on every `loop()` pass.

### Should it run now?

```cpp
bool scheduled = task.scheduled();     // enabled && !paused
bool due       = task.shouldRun();     // scheduled && interval elapsed
uint32_t left  = task.remainingTime(); // ms until the next run (0 if due or interval 0)
```

`remainingTime()` returns `0` when no interval is configured, and also once the task is due or overdue.

!!! warning "`remainingTime()` and `shouldRun()` disagree for a never-run task"
A task that has never run has `lastEndTime() == 0`, which `shouldRun()` treats as **due now**. `remainingTime()` does not apply that rule — it subtracts `millis() - 0`, i.e. the whole uptime — so it can report a non-zero value while the task is actually due:

```cpp
// task never ran, interval = 1000, board has been up for 500 ms
task.shouldRun();      // true  -> runs on the next loop()
task.remainingTime();  // 500   -> misleading, the task is not waiting
```

Do not use `remainingTime()` to decide whether a task will run; use `shouldRun()` (or `scheduled()` plus your own accounting).

### Immediate run

```cpp
task.forceRun(); // run right now, ignoring pause, enable state and interval
```

`forceRun()` bypasses all guards — including the enabled predicate — but still records statistics and fires `onDone()`.

### Early run

```cpp
task.requestEarlyRun(); // next tryRun() will consider the interval elapsed
```

Unlike `forceRun()`, an early run still honours `paused()` and `enabled()`. It is useful when an event downstream of the task means the next scheduled pass is now useless.

### Delayed resume

`resume(delayMillis)` both un-pauses the task and re-bases its interval clock, which is the idiomatic way to schedule a "run in N ms" task:

```cpp
task.resume(10000); // un-pause and run 10 seconds from now
```

!!! warning "`resume(delay)` permanently overwrites the interval"
`resume(delayMillis)` calls `setInterval(delayMillis)`, so the task's configured period is replaced. A later `resume()` with no argument does **not** restore it:

```cpp
task.setInterval(1000);
task.resume(5000); // interval is now 5000, not 1000
task.resume();     // interval is still 5000
```

If you only want a one-off deferred run and need to keep the period, save it first and restore it in the completion callback, or use a dedicated `ONCE` task.

## Passing Data

When `MYCILA_TASK_MANAGER_NO_PARAMS` is **not** defined, the task function receives a `void*`:

```cpp
const char* message = "Pong";

Mycila::Task ping("ping", [](void* params) {
  Serial.println((const char*)params);
});

ping.setData((void*)message);
ping.setType(Mycila::Task::Type::ONCE);
ping.resume();
```

`setData()` stores the pointer; the library never owns or frees it. Keep the pointee alive as long as the task may run — a string literal, a global, or a `static` buffer. Never pass the address of a local variable.

## Conditional Execution

Instead of toggling a task manually, attach a predicate:

```cpp
bool systemReady = false;

Mycila::Task announce("announce", [](void* params) {
  Serial.println("System is ready");
});

announce.setEnabledWhen([]() { return systemReady; });
announce.setInterval(1000);
```

The predicate is evaluated on every scheduling decision. It must therefore be fast and side-effect free.

!!! tip "Predicate vs. pause"
`setEnabled(false)` installs an internal always-false predicate; `pause()` is a separate flag. Use the predicate for *external* conditions and `pause()` for *internal* control flow (e.g. `ONCE` completion).

## Completion Callbacks

```cpp
sayHello.onDone([](const Mycila::Task& me, uint32_t elapsed) {
  ESP_LOGD("app", "Task '%s' executed in %" PRIu32 " ms", me.name(), elapsed);
  ping.resume(); // chain another task
});
```

`elapsed` is the raw execution time in **milliseconds** (`millis()` difference), independent of any profiling configuration. The callback runs synchronously at the end of the task, still inside `_run()`, and can safely change the state of other tasks (but not of the task currently running).

Disable callbacks entirely with `-D MYCILA_TASK_MANAGER_NO_CALLBACKS`.

## Pause, Resume and Enable

```cpp
task.pause();            // skip runs until resumed
task.resume();           // resume immediately
task.resume(5000);       // resume, first run in 5 s
task.setEnabled(false);  // equivalent to an always-false predicate
task.setEnabled(true);   // re-enable
```

| Query          | Returns true when                                  |
| :------------- | :------------------------------------------------- |
| `enabled()`    | the enabled predicate is absent or returns `true`  |
| `paused()`     | the task is paused (including a finished `ONCE`)   |
| `scheduled()`  | `enabled() && !paused()`                           |
| `shouldRun()`  | `scheduled()` and the interval has elapsed         |
| `running()`    | the task function is currently executing           |
| `earlyRunRequested()` | `lastEndTime() == 0`                        |

## Running a Task

A task can be driven on its own, without a manager:

```cpp
Mycila::Task task("task", [](void* params) {
  // ...
});
task.setInterval(100);

void loop() {
  task.tryRun(); // returns true if the task was executed
}
```

`tryRun()` is also what `TaskManager::loop()` calls for you — see [Task Managers](task-managers.md).
