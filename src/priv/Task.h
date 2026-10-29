// SPDX-License-Identifier: MIT
/*
 * Copyright (C) Mathieu Carbou
 */
#pragma once

#include <esp32-hal.h>
#include <freertos/FreeRTOS.h>

#ifdef MYCILA_JSON_SUPPORT
  #include <ArduinoJson.h>
#endif

#include "BinStatistics.h"

#include <cassert>
#include <functional>
#include <utility>

namespace Mycila {
  class Task {
    public:
      enum class Type {
        // once enabled, the task will run once and then pause itself
        ONCE,
        // the task will run at the specified interval as long as it is enabled
        FOREVER
      };

      enum class State : uint8_t {
        // the task is not executing anything
        IDLE,
        // the task function is executing
        RUNNING,
        // the task function returned and its onDone callback is executing
        FINALIZING
      };

#ifndef MYCILA_TASK_MANAGER_NO_PARAMS
      typedef std::function<void(void* params)> Function;
#else
      typedef std::function<void()> Function;
#endif
      typedef std::function<void(const Task& me, uint32_t elapsed)> DoneCallback;
      typedef std::function<bool()> Predicate;

      Task(const char* name, Function fn) : Task(name, Type::FOREVER, std::move(fn)) {}

      Task(const char* name, Type type, Function fn) : _name(name), _fn(std::move(fn)) {
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
        _enabled = std::move(predicate);
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

#ifndef MYCILA_TASK_MANAGER_NO_CALLBACKS
      // callback when the task is done
      Task& onDone(DoneCallback doneCallback) {
        _onDone = std::move(doneCallback);
        return *this;
      }
#endif

#ifndef MYCILA_TASK_MANAGER_NO_PARAMS
      // pass some data to the task
      Task& setData(void* params) {
        _params = params;
        return *this;
      }
      void* data() const { return _params; }
#endif

      // pause a task
      Task& pause() {
        _paused = true;
        return *this;
      }
      // check if the task is temporarily paused
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
      uint32_t remainingTime() const {
        if (!_intervalMs)
          return 0;
        uint32_t diff = millis() - _lastEnd;
        return diff >= _intervalMs ? 0 : _intervalMs - diff;
      }

      // get the last task end time in milliseconds
      // 0 means the task has never run yet or an early run was requested
      uint32_t lastEndTime() const { return _lastEnd; }

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
      // get the execution state of the task
      State state() const { return _state; }
      // check if the task function is currently executing (the onDone callback is not part of it)
      // since v5, running() is false while onDone executes: in v4 it was also false, but the
      // callback was not counted at all, so polling running() could exit before the task had
      // finished touching shared resources. use active() to wait for the whole execution.
      bool running() const { return _state == State::RUNNING; }
      // check if the task function or its onDone callback is currently executing
      // this is the condition to wait for when the task must not touch shared resources anymore
      bool active() const { return _state != State::IDLE; }
      // check if the task is not executing anything
      bool idle() const { return _state == State::IDLE; }

      // request an early run of the task and do not wait for the interval to be reached
      Task& requestEarlyRun() {
        _lastEnd = 0;
        return *this;
      }
      // check if the task is requested to run earlier than its scheduled interval
      bool earlyRunRequested() const { return _lastEnd == 0; }

      // enable profiling of the task
      // binCount is the number of bins to record the number of iterations in each bin.
      // unitDivider is the divider to use for the unit: 1 for milliseconds, 1000 for seconds, etc
      void enableProfiling(uint8_t binCount = 10, uint32_t unitDividerMillis = 1) {
#ifndef MYCILA_TASK_MANAGER_NO_STATS
        if (!_stats)
          _stats = new BinStatistics(binCount, unitDividerMillis);
#endif
      }
      void disableProfiling() {
#ifndef MYCILA_TASK_MANAGER_NO_STATS
        if (_stats) {
          delete _stats;
          _stats = nullptr;
        }
#endif
      }

      bool profiled() const {
#ifndef MYCILA_TASK_MANAGER_NO_STATS
        return _stats;
#else
        return false;
#endif
      }
#ifndef MYCILA_TASK_MANAGER_NO_STATS
      const BinStatistics* statistics() const { return _stats; }
#endif

      Task& log();

#ifdef MYCILA_JSON_SUPPORT
      void toJson(const JsonObject& root) const;
#endif

    private:
      const char* _name;
      const Function _fn;

      Predicate _enabled = nullptr;
      // _paused, _state and TaskManager::_allowedToRun are volatile because they are read from
      // another FreeRTOS task than the one writing them. volatile prevents the compiler from
      // caching them in a register; it does not make a read-modify-write sequence atomic.
      volatile bool _paused = false;
      volatile State _state = State::IDLE;
      Type _type = Type::FOREVER;
      uint32_t _intervalMs = 0;
      // written by the worker, read by the application: not volatile because polling it from
      // another task is inherently racy, use state()/active() as the execution signal instead
      uint32_t _lastEnd = 0;
#ifndef MYCILA_TASK_MANAGER_NO_STATS
      Mycila::BinStatistics* _stats = nullptr;
#endif
#ifndef MYCILA_TASK_MANAGER_NO_CALLBACKS
      DoneCallback _onDone = nullptr;
#endif
#ifndef MYCILA_TASK_MANAGER_NO_PARAMS
      void* _params = nullptr;
#endif

      void _run(uint32_t now) {
        // a ONCE task is paused before the function runs, not after: the worker is then the first
        // writer of _paused for this execution, so a resume() from another FreeRTOS task during the
        // run (including from the function body) is not overwritten and the task runs again.
        // Placing this before the state store also orders the two writes as observed from other cores.
        if (_type == Type::ONCE)
          _paused = true;
        _state = State::RUNNING;
#ifndef MYCILA_TASK_MANAGER_NO_PARAMS
        _fn(_params);
#else
        _fn();
#endif
        // the task code is done, but the execution is not: everything below can still touch shared resources
        _state = State::FINALIZING;
        _lastEnd = millis();
#if !defined(MYCILA_TASK_MANAGER_NO_STATS) || !defined(MYCILA_TASK_MANAGER_NO_CALLBACKS)
        uint32_t elapsed = _lastEnd - now;
#endif
#ifndef MYCILA_TASK_MANAGER_NO_STATS
        if (_stats)
          _stats->record(elapsed);
#endif
#ifndef MYCILA_TASK_MANAGER_NO_CALLBACKS
        if (_onDone)
          _onDone(*this, elapsed);
#endif
        _state = State::IDLE;
      }
  };
} // namespace Mycila
