# Profiling & Statistics

The library records execution times into **power-of-2 histograms** (`Mycila::BinStatistics`). It is cheap, allocation-free at record time, and answers the question that matters on a scheduler: *"how often is a task fast, and how often does it blow past its budget?"*

## Enabling Profiling

### For a manager and all its tasks

```cpp
tasks.enableProfiling(
  6,  // taskManagerBinCount: bins for the manager's own loop() duration
  10, // taskBinCount: bins for each individual task
  1   // unitDividerMillis: 1 = milliseconds (default), 1000 = seconds
);
```

Call it **after** adding the tasks: it walks the current task list. Tasks added later are not profiled automatically.

### For a manager only

```cpp
tasks.enableProfiling(12); // one argument = manager bins only
```

!!! warning "Two arguments is ambiguous — always pass three"
The two `TaskManager::enableProfiling()` overloads overlap:

```cpp
void enableProfiling(uint8_t taskManagerBinCount, uint8_t taskBinCount, uint32_t unitDividerMillis = 1);
void enableProfiling(uint8_t taskManagerBinCount = 12, uint32_t unitDividerMillis = 1);
```

A two-argument call such as `enableProfiling(6, 10)` matches both and **fails to compile** with *"call to member function 'enableProfiling' is ambiguous"*. Pass either one argument, or all three:

```cpp
tasks.enableProfiling(6);         // OK: manager only
tasks.enableProfiling(6, 10, 1);  // OK: manager + tasks
```

### For a single task

```cpp
task.enableProfiling(10, 1);
```

!!! note "`enableProfiling()` does not reconfigure"
Statistics are allocated **only if none exist yet**. Calling `enableProfiling()` again with different parameters is a no-op — the original bin count and divider are kept, both for individual tasks and for the manager. To change the parameters, call `disableProfiling()` first, then enable profiling again.

### Disabling

```cpp
task.disableProfiling();
tasks.disableProfiling(); // manager + all tasks
```

## Reading the Statistics

```cpp
if (task.profiled()) {
  const Mycila::BinStatistics* stats = task.statistics();

  uint32_t count        = stats->count();        // total recorded runs
  uint8_t  bins         = stats->bins();         // number of bins
  uint32_t unitDivider  = stats->unitDivider();  // divisor applied to elapsed (ms)
  uint16_t bin3         = stats->bin(3);         // runs falling in bin 3
}
```

`statistics()` is only available when `MYCILA_TASK_MANAGER_NO_STATS` is not defined.

## How the Bins Work

With `binCount` bins, a recorded elapsed time `t` (already divided by `unitDivider`) lands in:

| Bin   | Range                    |
| :---- | :----------------------- |
| `0`   | `0 ≤ t < 2^1`            |
| `1`   | `2^1 ≤ t < 2^2`          |
| `2`   | `2^2 ≤ t < 2^3`          |
| …     | …                        |
| `n-2` | `2^(n-2) ≤ t < 2^(n-1)`  |
| `n-1` | `t ≥ 2^(n-1)`            |

Each bin saturates at `UINT16_MAX`; the total `count()` saturates at `UINT32_MAX` (and then resets to zero on the next record).

Example: `binCount = 10`, `unitDivider = 1` (milliseconds) gives 10 bins covering `0–1 ms` up to `≥512 ms`.

!!! note "The divider makes bins coarser, not finer"
`elapsed` is measured in **milliseconds** (`millis()`), and is divided by `unitDividerMillis` before being binned. So the divider can only widen the unit:

| `unitDividerMillis` | Unit of the bin ranges |
| :------------------ | :--------------------- |
| `1` (default)       | milliseconds           |
| `1000`              | seconds                |

Use `binCount` — not the divider — to control the resolution: with milliseconds and `binCount = 10`, the last bin already covers `≥ 512 ms`.

## Logging

```cpp
tasks.log();   // manager + all profiled tasks
task.log();    // one task
```

Logs use the `TASKS` tag and only print entries that have both bins and a non-zero count:

```text
I (5123) TASKS: |                         loop() | 3 < 2^1 | 12 < 2^2 | 0 < 2^3 | 0 < 2^4 | 0 < 2^5 | 0 >= 2^5 | count=15
I (5123) TASKS: |                       sayHello | 0 < 2^1 | 15 < 2^2 | 0 < 2^3 | 0 < 2^4 | 0 < 2^5 | 0 >= 2^5 | count=15
```

Read that as: `loop()` ran 15 times total, 3 passes took 0–1 ms (`< 2^1`) and 12 took 2–3 ms (`2^1 … 2^2`). Each bin `i` covers `2^i … 2^(i+1)` ms, so bins are contiguous but not "2–4 ms".

!!! note "`log()` needs enabled profiling and data"
`Task::log()` and `TaskManager::log()` print nothing for an entry that has no `BinStatistics` or whose `count()` is still `0`. Enable profiling on the tasks and let them run at least once.

### Periodic logging pattern

```cpp
tasks.newTask("stats", [](void* params) {
  tasks.log();
}).setInterval(5000);
```

## Resetting

```cpp
task.statistics()->clear();
```

## Compile-Time Removal

Define `MYCILA_TASK_MANAGER_NO_STATS` to strip all statistics code — no `BinStatistics`, no heap allocation, no `_stats` member:

```ini
build_flags = -D MYCILA_TASK_MANAGER_NO_STATS
```

In that configuration `profiled()` always returns `false` and `statistics()` is not compiled.
