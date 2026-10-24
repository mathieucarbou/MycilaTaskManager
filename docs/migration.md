# Migrating from v4 to v5

v5 fixes two long-standing defects and, because the fix changes what some methods actually do, it is a **major** release. Most code compiles unchanged; this page lists every behavioural difference so you can audit your call sites.

## Summary

| Change | Compile error? | Behaviour change? |
| :--- | :--- | :--- |
| `TaskManager::pause()` / `resume()` | no | **yes — audit every call** |
| `TaskManager::resume(delay)` | **yes** | — |
| `TaskManager::waitForAllTasksToComplete(ms)` | no | **yes — the argument changed meaning** |
| `Task::running()` | no | **yes — `onDone` no longer counted** |
| `Task::remainingTme()` | **yes** (renamed) | — |
| `TaskManager::pauseTasks()` / `resumeTasks()` | no | new names for the v4 broadcasts |
| `Task::state()`, `active()`, `idle()` | no | new |
| `TaskManager::state()`, `paused()`, `waitForPaused()` | no | new |

## `pause()` and `resume()` really pause the manager

In v4, `TaskManager::pause()` was implemented by calling `Task::pause()` on every task, while the manager kept dispatching. The name promised a boundary the code never provided: a callback, or its `onDone`, could resume a task and have it dispatched again right after `pause()` returned.

v5 implements what the name says:

- `pause()` closes the manager's **dispatch gate** and never touches a task
- `resume()` reopens it
- the old broadcast behaviour is available as `pauseTasks()` / `resumeTasks(delay)`

```cpp
// v4: pauses every task, manager keeps dispatching
tasks.pause();

// v5: pauses the manager, task states untouched
tasks.pause();

// v5: what the v4 call actually did
tasks.pauseTasks();
```

!!! danger "This change compiles silently"
`tasks.pause()` and `tasks.resume()` still compile, and on both versions they make the tasks stop running. The difference is what happens to *callbacks already executing* and to *state*:

| | v4 `pause()` | v5 `pause()` |
| :--- | :--- | :--- |
| A running callback finishes | yes | yes |
| Its `onDone` can resume another task which then runs | **yes** | **no** |
| Pending `ONCE` tasks stay pending after `resume()` | no (everything resumed) | **yes** |
| Intervals and timestamps preserved | no (re-based by `resume(ms)`) | **yes** |

**Audit rule:** if you used `pause()` to quiesce before exclusive access (OTA, reset, shared resource), the v5 behaviour is what you actually needed — keep the call and consider `waitForPaused()` to wait for the confirmation. If you used `pause()` to stop every task and later resumed them on purpose, switch to `pauseTasks()` / `resumeTasks()`.

`resume()` no longer takes a delay, so `tasks.resume(1000)` fails to compile:

```cpp
tasks.resume(1000);        // v4: resume every task, first run in 1 s — ERROR in v5
tasks.resumeTasks(1000);   // v5 equivalent
```

## `waitForAllTasksToComplete(ms)`: the argument changed meaning

```cpp
void waitForAllTasksToComplete(uint32_t intervalMs = 10) const;  // v4: poll interval
bool waitForAllTasksToComplete(uint32_t timeoutMs = 0) const; // v5: timeout, 0 = wait forever
```

The signature is source-compatible, so `tasks.waitForAllTasksToComplete(50)` compiles on both versions — but on v4 `50` means *sleep 50 ms between two checks* (wait forever), while on v5 it means *give up after 50 ms*. A ported call can now return early with `false` where it used to block until completion.

```cpp
tasks.waitForAllTasksToComplete(50); // v4: poll every 50 ms, block until done
tasks.waitForAllTasksToComplete(50); // v5: wait at most 50 ms, may return false

// v5 equivalent of the v4 call:
tasks.waitForAllTasksToComplete();   // blocks until done (0 = wait forever)
```

The method also now returns `bool` (`true` = all tasks idle). Existing calls that ignore the result are unaffected.

Two further changes, both fixes:

- it waits for `onDone` to finish too (v4 could return while a callback was still running)
- it returns immediately instead of deadlocking when called from the manager's worker or from one of its callbacks

## `running()` no longer covers `onDone`

In v4, `_running` was cleared **before** the `onDone` callback ran, so `running()` was `false` while the callback was still executing. v5 exposes the real execution state:

```cpp
enum class State : uint8_t { IDLE, RUNNING, FINALIZING };
```

- `running()` is now strictly `state() == RUNNING` — the task function only
- `active()` (new) is `state() != IDLE` — function **or** `onDone`; this is the condition to wait for when a task must stop touching a shared resource
- `idle()` (new) is `state() == IDLE`

```cpp
while (task.running()) {} // v4: could exit while onDone is still running
while (task.active()) {}  // v5: waits for the whole execution
```

## `remainingTme()` renamed

The typo is fixed:

```cpp
task.remainingTme();  // v4 — compile error in v5
task.remainingTime(); // v5
```

## New in v5, no migration needed

- `TaskManager::State { RUNNING, PAUSING, PAUSED }` with `state()`, `paused()`, `waitForPaused(timeout)` — the dispatch gate
- `pauseTasks()` / `resumeTasks(delay)` — the v4 broadcasts, under accurate names
- `Task::State { IDLE, RUNNING, FINALIZING }` with `state()`, `active()`, `idle()`
- a `state` field in the JSON output, for both the manager and each task
- the `PauseResume` example demonstrating the issue #18 scenario

## No API changes

Everything else is signature- and behaviour-compatible: `Task` construction, `setType()`, `setEnabled()` / `setEnabledWhen()`, `setInterval()`, `onDone()`, `setData()`, `pause()` / `resume(delay)` on **Task** (the per-task methods are untouched), `forceRun()`, `tryRun()`, `requestEarlyRun()`, `scheduled()`, `shouldRun()`, profiling, `newTask()`, `addTask()`, `removeTask()`, `loop()`, `setEnabled()`, `asyncStart()`, `asyncStop()`, `configureWDT()`, `log()`, `toJson()`.

## Note on library layout

The headers moved but the public include is unchanged — `#include <MycilaTaskManager.h>` still works, and `Task`, `TaskManager` and `BinStatistics` are still in namespace `Mycila`. Internal headers now live in `src/priv/`.