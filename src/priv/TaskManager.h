// SPDX-License-Identifier: MIT
/*
 * Copyright (C) Mathieu Carbou
 */
#pragma once

#include <esp32-hal.h>
#include <esp_task_wdt.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#ifdef MYCILA_JSON_SUPPORT
  #include <ArduinoJson.h>
#endif

#include "BinStatistics.h"
#include "Task.h"

#include <cassert>
#include <functional>
#include <list>
#include <memory>
#include <utility>

namespace Mycila {
  class TaskManager {
    public:
      enum class State : uint8_t {
        // dispatching tasks, or ready to
        RUNNING,
        // a pause was requested and an execution (task code, statistics and onDone) is still in flight
        PAUSING,
        // pause confirmed: no callback of this manager is active and none will start until resume()
        PAUSED
      };

      explicit TaskManager(const char* name) : _name(name) {}

      ~TaskManager();

      const char* name() const { return _name; }

      Task& newTask(const char* name, Task::Function fn) {
        return newTask(name, Task::Type::FOREVER, fn);
      }

      Task& newTask(const char* name, Task::Type type, Task::Function fn);

      void addTask(Task& task); // NOLINT

      void removeTask(Task& task); // NOLINT

      // number of tasks
      size_t tasks() const { return _tasks.size(); }
      bool empty() const { return _tasks.empty(); }

      // Must be called from main loop and will loop over all registered tasks.
      // When using async mode, do not call loop: the async task will call it.
      // Returns the number of executed tasks
      // @param delayMillis: if > 0, will delay this amount of milliseconds at the end of the loop
      size_t loop(uint32_t delayMillis = 10);

      // Wait until no task of this manager is executing: the task code, the statistics recording
      // and the onDone callback are all done, so no callback of this manager can still touch a
      // shared resource. Returns true when every task is idle, false on timeout.
      // @param timeoutMs: maximum time to wait, 0 means wait forever (like loop(0) means no delay).
      // Since v5 this is a timeout: in v4 the argument was the polling interval and the method
      // waited forever. A ported call like waitForAllTasksToComplete(50) now gives up after 50 ms
      // instead of polling every 50 ms.
      // This does not prevent the manager from dispatching a task that a callback already resumed:
      // use pause() + waitForPaused() for a dispatch boundary.
      // Returns immediately when called from this manager's worker or from one of its callbacks,
      // because waiting there can never succeed.
      bool waitForAllTasksToComplete(uint32_t timeoutMs = 0) const;

      // Execution state of the manager.
      // RUNNING: dispatching tasks (or ready to).
      // PAUSING: a pause was requested but an execution is still in flight.
      // PAUSED: no callback of this manager is active and none will start until resume().
      State state() const {
        portENTER_CRITICAL(&_mux);
        State s = !_pauseRequested ? State::RUNNING : (!_pauseAcknowledged ? State::PAUSING : State::PAUSED);
        portEXIT_CRITICAL(&_mux);
        return s;
      }

      // check if the manager is confirmed paused
      bool paused() const { return state() == State::PAUSED; }

      // Request the manager to stop dispatching tasks. Does not block and does not touch any task:
      // the current execution (task code, statistics and onDone) finishes, then the manager stops at
      // the next execution boundary. Call paused() or waitForPaused() to wait for the confirmation:
      // only a confirmed pause guarantees that no callback is active anymore.
      // This can be called from any FreeRTOS task, including a callback of this manager.
      // This is the manager counterpart of pauseTasks(), which pauses every task instead.
      // v5: this is the v4 name with a new meaning. The v4 implementation paused every task while
      // the manager kept dispatching; that broadcast is now pauseTasks().
      void pause();

      // Resume a paused or pausing manager. Tasks are left untouched: pending tasks stay pending,
      // completed ONCE tasks stay paused, per-task pause state, intervals and timestamps are preserved.
      // Can be called from any FreeRTOS task.
      void resume();

      // Wait until the pause is confirmed (state() == State::PAUSED).
      // Returns true if the manager is paused, false on timeout.
      // @param timeoutMs: maximum time to wait, 0 means wait forever.
      // Always returns immediately if called from a task of this manager: waiting for a pause from
      // inside the worker or from a callback would never be confirmed.
      bool waitForPaused(uint32_t timeoutMs = 0) const;

      // call Task::pause() on every task.
      // This is a per-task operation: it does not stop the manager from dispatching tasks.
      void pauseTasks();

      // call Task::resume() on every task.
      // This is a per-task operation: it resumes every task, including the ones which were
      // intentionally paused or already completed.
      void resumeTasks(uint32_t delayMillis = 0);

      void setEnabled(bool enabled) {
        for (auto& task : _tasks)
          task->setEnabled(enabled);
      }

      // enable profiling for all tasks, plus the task manager itself
      // unitDivider is the divider to use for the unit: 1 for milliseconds, 1000 for seconds, etc
      void enableProfiling(uint8_t taskManagerBinCount, uint8_t taskBinCount, uint32_t unitDividerMillis = 1) {
#ifndef MYCILA_TASK_MANAGER_NO_STATS
        if (!_stats)
          _stats = new BinStatistics(taskManagerBinCount, unitDividerMillis);
        for (auto& task : _tasks)
          task->enableProfiling(taskBinCount, unitDividerMillis);
#endif
      }

      // enable profiling for the task manager only
      void enableProfiling(uint8_t taskManagerBinCount = 12, uint32_t unitDividerMillis = 1) {
#ifndef MYCILA_TASK_MANAGER_NO_STATS
        if (!_stats)
          _stats = new BinStatistics(taskManagerBinCount, unitDividerMillis);
#endif
      }

      // disable profiling for all tasks, plus the task manager itself
      void disableProfiling() {
#ifndef MYCILA_TASK_MANAGER_NO_STATS
        if (_stats) {
          delete _stats;
          _stats = nullptr;
        }
        for (auto& task : _tasks)
          task->disableProfiling();
#endif
      }

      // log all tasks
      void log();

      // json output of the task manager
#ifdef MYCILA_JSON_SUPPORT
      void toJson(const JsonObject& root) const;
#endif

      // Start the task manager in a separate task.
      // - You can add a delay in milliseconds when no task is executed
      // - You can also enable the global Watchdog Timer (WDT) for this task manager
      // - If core ID is not set (-1), then the task will run on the same core as the caller
      // - If priority is not set (-1), then the task will run with the same priority as the caller
      bool asyncStart(uint32_t stackSize = 4096,
                      BaseType_t priority = -1,
                      BaseType_t coreID = -1,
                      uint32_t delay = 10,
                      bool wdt = false);

      // kill the async task
      void asyncStop();

      // Initialize the global Task Watchdog Timer (TWDT)
      // Ref: https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/system/wdts.html
      // Returns true if the WDT was configured or reconfigured successfully
      static bool configureWDT(uint32_t timeoutSeconds = CONFIG_ESP_TASK_WDT_TIMEOUT_S, bool panic = true);

    private:
      const char* _name;
      std::list<std::shared_ptr<Task>> _tasks;
      bool _wdt = false;

#ifndef MYCILA_TASK_MANAGER_NO_STATS
      Mycila::BinStatistics* _stats = nullptr;
#endif

      static void doNotDelete(Mycila::Task* t) {}

      // async
      TaskHandle_t _taskManagerHandle = NULL;
      uint32_t _delay = 0;
      // stop request: written by asyncStop() from the caller's task, read by the worker loop
      volatile bool _allowedToRun = false;

      // pause gate: guarded by _mux because it sequences the dispatching thread and the caller.
      // _pauseRequested is also read outside the critical section, between two task executions,
      // so it must be volatile: the compiler is not allowed to keep it in a register across the loop.
      mutable portMUX_TYPE _mux = portMUX_INITIALIZER_UNLOCKED;
      volatile bool _pauseRequested = false;
      bool _pauseAcknowledged = false;
      bool _dispatching = false;

      // wake-up primitive so that resume() and asyncStop() do not wait for the worker delay
      SemaphoreHandle_t _wakeUp = nullptr;
      // handle of the context currently driving loop(), when it is not the async worker
      volatile TaskHandle_t _self = NULL;

      void _notify() {
        if (_wakeUp)
          xSemaphoreGive(_wakeUp);
      }

      // true when the caller is the context driving this manager, i.e. waiting would deadlock
      bool _isSelf() const {
        const TaskHandle_t self = xTaskGetCurrentTaskHandle();
        return self == _taskManagerHandle || self == _self;
      }

      bool _allIdle() const {
        for (const auto& task : _tasks) {
          if (task->active())
            return false;
        }
        return true;
      }

      // Pace the dispatching context: sleep for delayMillis, interruptible by resume() or asyncStop().
      // Only the context driving the manager may use this: it consumes the wake-up signal, so it must
      // never be called from a concurrent waiter (see _sleep() for that).
      // pdMS_TO_TICKS() is used instead of Arduino's delay() because delay() truncates with an integer
      // division: delay(1) becomes vTaskDelay(0) on any build where portTICK_PERIOD_MS > 1 (e.g.
      // configTICK_RATE_HZ < 1000), which does not block at all and turns the caller into a busy loop.
      // pdMS_TO_TICKS() truncates the same way, so a zero tick count is forced to 1.
      void _wait(uint32_t delayMillis) const {
        if (!delayMillis)
          return;
        const TickType_t ticks = _ticks(delayMillis);
        if (_wakeUp)
          xSemaphoreTake(_wakeUp, ticks);
        else
          vTaskDelay(ticks);
      }

      // Polling sleep for a waiter that is not the dispatching context. Uses vTaskDelay() and never
      // consumes the wake-up semaphore, so it cannot steal the signal meant for the worker.
      static void _sleep(uint32_t delayMillis) {
        if (!delayMillis)
          return;
        vTaskDelay(_ticks(delayMillis));
      }

      // convert milliseconds to ticks, never returning 0 so the caller always blocks at least one tick
      static TickType_t _ticks(uint32_t delayMillis) {
        const TickType_t ticks = pdMS_TO_TICKS(delayMillis);
        return ticks ? ticks : 1;
      }

      static void _asyncTaskManager(void* params) {
        TaskManager* taskManager = reinterpret_cast<TaskManager*>(params);
        taskManager->_allowedToRun = true;
        while (taskManager->_allowedToRun) {
          // loop() acknowledges a pending pause at the execution boundary and returns 0
          taskManager->loop(0);
          if (taskManager->_wdt)
            esp_task_wdt_reset(); // also fed while parked, a deliberate pause must not trip the WDT
          taskManager->_wait(taskManager->_delay);
        }
        vTaskDelete(NULL);
      }

    private:
      friend class Task;
  };
} // namespace Mycila
