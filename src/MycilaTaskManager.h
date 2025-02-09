// SPDX-License-Identifier: MIT
/*
 * Copyright (C) 2023-2024 Mathieu Carbou
 */
#pragma once

#include <Print.h>
#include <esp_task_wdt.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#ifdef MYCILA_JSON_SUPPORT
  #include <ArduinoJson.h>
#endif

#include "MycilaBinStatistics.h"

#include <functional>
#include <list>
#include <memory>

#define MYCILA_TASK_MANAGER_VERSION          "3.2.1"
#define MYCILA_TASK_MANAGER_VERSION_MAJOR    3
#define MYCILA_TASK_MANAGER_VERSION_MINOR    2
#define MYCILA_TASK_MANAGER_VERSION_REVISION 1

namespace Mycila {
  class Task {
    public:
      enum class Type {
        // once enabled, the task will run once and then pause itself
        ONCE,
        // the task will run at the specified interval as long as it is enabled
        FOREVER
      };

      typedef std::function<void(void* params)> Function;
      typedef std::function<void(const Task& me, uint32_t elapsed)> DoneCallback;
      typedef std::function<bool()> Predicate;

      Task(const char* name, Function fn) : Task(name, Type::FOREVER, fn) {}

      Task(const char* name, Type type, Function fn) : _name(name), _fn(fn) {
        assert(_name);
        assert(_fn);
        setType(type);
      }

      ~Task();

      const char* name() const { return _name; }

      // change task type.
      Task& setType(Type type) {
        if (_type == type)
          return *this;
        _type = type;
        // if the task is a ONCE task, it starts paused by default
        _paused = _type == Type::ONCE;
        return *this;
      }
      Type type() const { return _type; }

      // change the enabled state
      Task& setEnabled(bool enabled);
      Task& setEnabledWhen(Predicate predicate) {
        _enabled = predicate;
        return *this;
      }
      // check if a task is enabled as per the enabled predicate. By default a task is enabled.
      bool enabled() const { return !_enabled || _enabled(); }

      // change the interval of execution
      Task& setInterval(uint32_t intervalMillis) {
        _intervalMs = intervalMillis;
        return *this;
      }
      // task interval in milliseconds
      uint32_t interval() const { return _intervalMs; }

      // callback when the task is done
      Task& onDone(DoneCallback doneCallback) {
        _onDone = doneCallback;
        return *this;
      }

      // pass some data to the task
      Task& setData(void* params) {
        _params = params;
        return *this;
      }
      void* data() const { return _params; }

      // pause a task
      Task& pause() {
        _paused = true;
        return *this;
      }
      // check is the task is temporary paused
      bool paused() const { return _paused; }
      // resume a paused task
      // if delayMillis is set, the task will resume after the delay
      Task& resume(uint32_t delayMillis = 0) {
        if (delayMillis) {
          setInterval(delayMillis);
          _lastEnd = millis();
        }
        _paused = false;
        return *this;
      }

      // get remaining time before next run in milliseconds
      uint32_t remainingTme() const {
        if (!_intervalMs)
          return 0;
        uint32_t diff = millis() - _lastEnd;
        return diff >= _intervalMs ? 0 : _intervalMs - diff;
      }

      // check if the task is scheduled to be run, meaning it is enabled, not paused and the interval will be reached
      bool scheduled() const { return enabled() && !_paused; }

      // check if the task should run, meaning it is enabled, not paused and the interval has been reached
      bool shouldRun() const {
        if (_paused || !enabled())
          return false;
        return _lastEnd == 0 || _intervalMs == 0 || millis() - _lastEnd >= _intervalMs;
      }

      // try to run the task if it should run
      bool tryRun() {
        if (_paused || !enabled())
          return false;
        if (_lastEnd == 0 || _intervalMs == 0) {
          _run(millis());
          return true;
        }
        uint32_t now = millis();
        if (now - _lastEnd >= _intervalMs) {
          _run(now);
          return true;
        }
        return false;
      }
      // force the task to run
      Task& forceRun() {
        _run(millis());
        return *this;
      }
      // check if the task is currently running
      bool running() const { return _running; }

      // request an early run of the task and do not wait for the interval to be reached
      Task& requestEarlyRun() {
        _lastEnd = 0;
        return *this;
      }
      // check if the task is requested to run earlier than its scheduled interval
      bool earlyRunRequested() const { return _lastEnd == 0; }

      // enable profiling of the task
      // binCount is the number of bins to record the number of iterations in each bin.
      // unitDivider is the divider to se for the unit: 1 for milliseconds, 1000 for seconds, etc
      void enableProfiling(uint8_t binCount = 10, uint32_t unitDividerMillis = 1) {
        if (!_stats)
          _stats = new BinStatistics(binCount, unitDividerMillis);
      }
      void disableProfiling() {
        if (_stats) {
          delete _stats;
          _stats = nullptr;
        }
      }

      bool profiled() const { return _stats; }
      const BinStatistics* statistics() const { return _stats; }

      Task& log();

#ifdef MYCILA_JSON_SUPPORT
      void toJson(const JsonObject& root) const {
        root["name"] = _name;
        root["type"] = _type == Type::ONCE ? "ONCE" : "FOREVER";
        root["paused"] = _paused;
        root["enabled"] = enabled();
        root["interval"] = _intervalMs;
        if (_stats)
          _stats->toJson(root["stats"].to<JsonObject>());
      }
#endif

    private:
      const char* _name;
      const Function _fn;

      Predicate _enabled = nullptr;
      bool _paused = false;
      bool _running = false;
      DoneCallback _onDone = nullptr;
      Mycila::BinStatistics* _stats = nullptr;
      Type _type = Type::FOREVER;
      uint32_t _intervalMs = 0;
      uint32_t _lastEnd = 0;
      void* _params = nullptr;

      void _run(uint32_t now) {
        _running = true;
        _fn(_params);
        _running = false;
        _lastEnd = millis();
        if (_type == Type::ONCE)
          _paused = true;
        uint32_t elapsed = _lastEnd - now;
        if (_stats)
          _stats->record(elapsed);
        if (_onDone)
          _onDone(*this, elapsed);
      }
  };

  class TaskManager {
    public:
      explicit TaskManager(const char* name) : _name(name) {}

      ~TaskManager() { _tasks.clear(); }

      const char* name() const { return _name; }

      Task& newTask(const char* name, Task::Function fn) {
        return newTask(name, Task::Type::FOREVER, fn);
      }

      Task& newTask(const char* name, Task::Type type, Task::Function fn) {
        auto task = std::make_shared<Task>(name, type, fn);
        _tasks.push_back(task);
        return *task;
      }

      void addTask(Task& task) { // NOLINT
        _tasks.push_back(std::shared_ptr<Task>(&task, doNotDelete));
      }

      void removeTask(Task& task) { // NOLINT
        _tasks.remove_if([&task](const std::shared_ptr<Task>& t) { return t.get() == &task; });
      }

      // number of tasks
      size_t tasks() const { return _tasks.size(); }
      bool empty() const { return _tasks.empty(); }

      // Must be called from main loop and will loop over all registered tasks.
      // When using async mode, do not call loop: the async task will call it.
      // Returns the number of executed tasks
      size_t loop() {
        size_t executed = 0;
        uint32_t now = millis();
        for (auto& task : _tasks) {
          if (task->tryRun()) {
            executed++;
            yield();
          }
        }
        if (_stats && executed) {
          _stats->record(millis() - now);
        }
        return executed;
      }

      // call pause() on all tasks
      void pause() {
        for (auto& task : _tasks)
          task->pause();
      }

      // call resume() on all tasks
      void resume(uint32_t delayMillis = 0) {
        for (auto& task : _tasks)
          task->resume(delayMillis);
      }

      void setEnabled(bool enabled) {
        for (auto& task : _tasks)
          task->setEnabled(enabled);
      }

      // enable profiling for all tasks, plus the task manager itself
      // unitDivider is the divider to se for the unit: 1 for milliseconds, 1000 for seconds, etc
      void enableProfiling(uint8_t taskManagerBinCount, uint8_t taskBinCount, uint32_t unitDividerMillis = 1) {
        if (!_stats)
          _stats = new BinStatistics(taskManagerBinCount, unitDividerMillis);
        for (auto& task : _tasks)
          task->enableProfiling(taskBinCount, unitDividerMillis);
      }

      // enable profiling for the task manager only
      void enableProfiling(uint8_t taskManagerBinCount = 12, uint32_t unitDividerMillis = 1) {
        if (!_stats)
          _stats = new BinStatistics(taskManagerBinCount, unitDividerMillis);
      }

      // disable profiling for all tasks, plus the task manager itself
      void disableProfiling() {
        if (_stats) {
          delete _stats;
          _stats = nullptr;
        }
        for (auto& task : _tasks)
          task->disableProfiling();
      }

      // log all tasks
      void log();

      // json output of the task manager
#ifdef MYCILA_JSON_SUPPORT
      void toJson(const JsonObject& root) const {
        root["name"] = _name;
        if (_stats)
          _stats->toJson(root["stats"].to<JsonObject>());
        for (auto& task : _tasks)
          task->toJson(root["tasks"].add<JsonObject>());
      }
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
      Mycila::BinStatistics* _stats = nullptr;
      bool _wdt = false;

      static void doNotDelete(Mycila::Task* t) {}

      // async
      TaskHandle_t _taskManagerHandle = NULL;
      uint32_t _delay = 0;
      static void _asyncTaskManager(void* params) {
        TaskManager* taskManager = reinterpret_cast<TaskManager*>(params);
        while (true) {
          if (taskManager->_wdt)
            esp_task_wdt_reset();
          if (!taskManager->loop()) {
            if (taskManager->_delay)
              delay(taskManager->_delay);
            else
              yield();
          }
        }
        vTaskDelete(NULL);
      }

    private:
      friend class Task;
  };

} // namespace Mycila
