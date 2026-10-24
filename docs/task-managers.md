# Task Managers

`Mycila::TaskManager` groups tasks so that a single `loop()` drives them all, and so that pause, resume, profiling and JSON export apply to the whole group.

## Creating a Manager

```cpp
#include <MycilaTaskManager.h>

Mycila::TaskManager tasks("loop()");
```

The name is used in logs and in JSON output. A manager can hold tasks it **owns** (created through `newTask()`) and tasks it merely **references** (added through `addTask()`).

## Adding Tasks

### Owned tasks — `newTask()`

```cpp
tasks.newTask("hello", [](void* params) {
  Serial.println("Hello");
}).setInterval(1000);

tasks.newTask("boot", Mycila::Task::Type::ONCE, [](void* params) {
  Serial.println("Boot");
});
```

`newTask()` creates the task on the heap (owned by a `std::shared_ptr`) and returns a reference, so calls can be chained. The task is destroyed when the manager is destroyed. Note that `newTask()` returns a reference into the manager's list — it stays valid as long as the task is not removed.

### Referenced tasks — `addTask()` / `removeTask()`

```cpp
// file scope: the tasks outlive the manager's references to them
Mycila::Task sayHello("sayHello", [](void* params) { Serial.println("Hello"); });
Mycila::Task sayGoodbye("sayGoodbye", [](void* params) { Serial.println("Goodbye"); });

void setup() {
  tasks.addTask(sayHello);
  tasks.addTask(sayGoodbye);

  // later
  tasks.removeTask(sayGoodbye);
}
```

Referenced tasks live in the caller's scope (typically as globals or members) and are **not** deleted by the manager. Use this form when you need to call the task's methods directly from elsewhere.

!!! warning "Lifetime"
Adding a task by reference means the manager keeps a raw pointer. Removing it from the manager is mandatory before the task object goes out of scope.

!!! warning "Task is not copy-assignable"
`Task` holds a `const std::function`, so its copy assignment operator is deleted. The copy constructor is implicitly generated and **does** compile — but it is shallow: a profiled task copied this way shares the same `BinStatistics` pointer, and both destructors free it, causing a double free / crash.

```cpp
Mycila::Task a("a", fn);
Mycila::Task b("b", fn);

b = a;              // error: copy assignment operator is implicitly deleted
Mycila::Task c(a);  // compiles, but unsafe once profiling is enabled
```

Declare tasks as globals, members, or create them with `newTask()`.

!!! warning "Adding the same task twice"
`addTask()` does not check for duplicates. Adding a task twice makes `loop()` run it twice per pass, and `tryRun()` will still be gated by the interval — but the task appears twice in `log()` and in the JSON `tasks` array. `removeTask()` removes **all** matching entries.

## Driving the Tasks

```cpp
void loop() {
  tasks.loop();
}
```

`loop(uint32_t delayMillis = 10)` iterates over every task, calls `tryRun()` on it, yields after each executed task, records one manager profiling entry when at least one task ran, and finally delays by `delayMillis`.

!!! note "Implicit delay"
The default `delayMillis` is **10 ms**, so a bare `tasks.loop()` already throttles the loop to roughly 100 passes per second. Pass `loop(0)` to disable this delay.

Return value: the number of tasks that were executed during this pass.

```cpp
size_t executed = tasks.loop();
if (executed == 0) {
  // no task was due in this pass — this is normal, not an error
}

if (tasks.empty()) {
  // no task registered at all
}
```

## Pause, Resume and Enable

```cpp
tasks.pause();          // pause every task
tasks.resume();         // resume every task now
tasks.resume(1000);     // resume every task, first run in 1 s
tasks.setEnabled(false); // install a false predicate on every task
tasks.setEnabled(true);
```

These forward to each task, so a task's own state is overwritten. Any predicate previously installed with `setEnabledWhen()` is replaced by `setEnabled()`.

## Waiting for Completion

```cpp
tasks.waitForAllTasksToComplete();      // poll every 10 ms
tasks.waitForAllTasksToComplete(50);    // poll every 50 ms
```

This busy-waits while any task reports `running() == true`. It blocks the caller, so it is only appropriate during shutdown or when you knowingly need synchronous completion (e.g. flushing before deep sleep).

## Querying the Manager

```cpp
const char* name = tasks.name();
size_t count     = tasks.tasks();
bool none        = tasks.empty();
```

## Multiple Managers

Several managers can coexist, each with its own set of tasks. This is the cleanest way to give different subsystem their own scheduling policy — or their own core and priority:

```cpp
Mycila::TaskManager fastTasks("fast");
Mycila::TaskManager slowTasks("slow");

void loop() {
  fastTasks.loop(1);   // ~1 ms pacing
  slowTasks.loop(500); // ~500 ms pacing
}
```

The [Watchdog example](examples.md) uses two managers — one on `loop()`, one in a background task.

## Logging

```cpp
tasks.log();
```

Prints one line per task that has profiling enabled and recorded data, plus a line for the manager itself, using the `TASKS` log tag:

```text
I (5123) TASKS: |                         loop() | 3 < 2^1 | 12 < 2^2 | 0 < 2^3 | 0 < 2^4 | 0 < 2^5 | 0 >= 2^5 | count=15
I (5123) TASKS: |                       sayHello | 0 < 2^1 | 15 < 2^2 | 0 < 2^3 | 0 < 2^4 | 0 < 2^5 | 0 >= 2^5 | count=15
```

See [Profiling & Statistics](statistics.md) for how to configure the bins.
