// SPDX-License-Identifier: MIT
/*
 * Copyright (C) Mathieu Carbou
 */
#include <MycilaTaskManager.h>

#include <esp32-hal-log.h>

#include <memory>
#include <string>

#define TAG "TASKS"

Mycila::TaskManager::~TaskManager() {
  _tasks.clear();
#ifndef MYCILA_TASK_MANAGER_NO_STATS
  if (_stats)
    delete _stats;
#endif
  if (_wakeUp)
    vSemaphoreDelete(_wakeUp);
}

void Mycila::TaskManager::log() {
#ifndef MYCILA_TASK_MANAGER_NO_STATS
  if (_stats) {
    const uint8_t binCount = _stats->bins();
    const uint32_t count = _stats->count();
    if (binCount && count) {
      std::string line;
      line.reserve(192);
      for (uint8_t i = 0; i < binCount; i++) {
        line += " | ";
        line += std::to_string(_stats->bin(i));
        line += (i < binCount - 1 ? " < 2^" : " >= 2^");
        line += std::to_string(i < binCount - 1 ? (i + 1) : i);
      }
      line += " |";
      ESP_LOGI(TAG, "| %30s%s count=%" PRIu32, _name, line.c_str(), count);
    }
  }
  for (auto& task : _tasks)
    task->log();
#endif
}

Mycila::Task& Mycila::TaskManager::newTask(const char* name, Task::Type type, Task::Function fn) {
  auto task = std::make_shared<Task>(name, type, fn);
  _tasks.push_back(task);
  return *task;
}

void Mycila::TaskManager::addTask(Task& task) {
  _tasks.push_back(std::shared_ptr<Task>(&task, doNotDelete));
}

void Mycila::TaskManager::removeTask(Task& task) {
  _tasks.remove_if([&task](const std::shared_ptr<Task>& t) { return t.get() == &task; });
}

size_t Mycila::TaskManager::loop(uint32_t delayMillis) {
  size_t executed = 0;
  const bool self = _self == NULL;
  if (self)
    _self = xTaskGetCurrentTaskHandle();

  bool paused;
  {
    portENTER_CRITICAL(&_mux);
    paused = _pauseRequested;
    if (paused) {
      // confirmed here: we are between two executions, so nothing else is in flight
      _pauseAcknowledged = true;
    } else {
      _dispatching = true;
    }
    portEXIT_CRITICAL(&_mux);
  }

  if (!paused) {
#ifndef MYCILA_TASK_MANAGER_NO_STATS
    uint32_t now = millis();
#endif
    for (auto& task : _tasks) {
      // re-check between two executions: a task or its onDone can request the pause
      if (_pauseRequested)
        break;
      if (task->tryRun()) {
        executed++;
        yield();
      }
    }
#ifndef MYCILA_TASK_MANAGER_NO_STATS
    if (_stats && executed) {
      _stats->record(millis() - now);
    }
#endif
    portENTER_CRITICAL(&_mux);
    _dispatching = false;
    if (_pauseRequested)
      _pauseAcknowledged = true;
    portEXIT_CRITICAL(&_mux);
  }

  if (self)
    _self = NULL;
  // Pace the synchronous caller, whether tasks were dispatched or not: a paused manager must not
  // turn the caller's loop() into a busy loop. The async worker passes 0 and paces itself with the
  // interruptible _wait() in _asyncTaskManager(), so the delay is never applied twice.
  if (delayMillis)
    _wait(delayMillis);
  return executed;
}

bool Mycila::TaskManager::waitForAllTasksToComplete(uint32_t timeoutMs) const {
  if (_isSelf())
    return _allIdle();
  const uint32_t start = millis();
  while (!_allIdle()) {
    if (timeoutMs && millis() - start >= timeoutMs)
      return false;
    _sleep(10);
  }
  return true;
}

void Mycila::TaskManager::pause() {
  portENTER_CRITICAL(&_mux);
  _pauseRequested = true;
  // Dekker: if the worker is already inside its dispatch section, it owns the acknowledgement.
  // Otherwise it has already read _pauseRequested, or it will read it and acknowledge it itself.
  if (!_dispatching)
    _pauseAcknowledged = true;
  portEXIT_CRITICAL(&_mux);
  // wake the worker up if it is waiting between two passes
  _notify();
}

void Mycila::TaskManager::resume() {
  portENTER_CRITICAL(&_mux);
  _pauseRequested = false;
  _pauseAcknowledged = false;
  portEXIT_CRITICAL(&_mux);
  _notify();
}

bool Mycila::TaskManager::waitForPaused(uint32_t timeoutMs) const {
  if (_isSelf())
    return paused();
  const uint32_t start = millis();
  while (!paused()) {
    if (timeoutMs && millis() - start >= timeoutMs)
      return false;
    _sleep(1);
  }
  return true;
}

void Mycila::TaskManager::pauseTasks() {
  for (auto& task : _tasks)
    task->pause();
}

void Mycila::TaskManager::resumeTasks(uint32_t delayMillis) {
  for (auto& task : _tasks)
    task->resume(delayMillis);
}

bool Mycila::TaskManager::asyncStart(uint32_t stackSize, BaseType_t priority, BaseType_t coreID, uint32_t delay, bool wdt) {
  if (_taskManagerHandle)
    return false;

  _delay = delay;

  // created on demand: a manager which is also driven by loop() keeps its interruptible wait
  if (!_wakeUp)
    _wakeUp = xSemaphoreCreateBinary();

  if (coreID < 0)
    coreID = xPortGetCoreID();

  UBaseType_t prio = priority < 0 ? uxTaskPriorityGet(NULL) : priority;
  bool b = xTaskCreateUniversal(_asyncTaskManager, _name, stackSize, this, prio, &_taskManagerHandle, coreID) == pdPASS;

  if (b) {
    ESP_LOGD(TAG, "Task manager '%s' started async: core: %d, priority: %" PRIu32 ", stack: %" PRIu32 ", handle: %p", _name, coreID, prio, stackSize, _taskManagerHandle);

    if (wdt && esp_task_wdt_add(_taskManagerHandle) == ESP_OK) {
      ESP_LOGD(TAG, "Task manager '%s' added to WDT", _name);
    }
    _wdt = wdt;
  }
  return b;
}

void Mycila::TaskManager::asyncStop() {
  if (!_taskManagerHandle)
    return;
  ESP_LOGD(TAG, "Stopping async task manager with handle: %p", _taskManagerHandle);
  esp_task_wdt_delete(_taskManagerHandle);
  _allowedToRun = false;
  // lift a pending pause so that the worker reaches its exit check without waiting for the delay,
  // and so that a later asyncStart() does not restart in the paused state
  portENTER_CRITICAL(&_mux);
  _pauseRequested = false;
  _pauseAcknowledged = false;
  portEXIT_CRITICAL(&_mux);
  _notify();
  _taskManagerHandle = NULL;
}

bool Mycila::TaskManager::configureWDT(uint32_t timeoutSeconds, bool panic) {
  ESP_LOGI(TAG, "Configuring Task Watchdog Timer (TWDT) to %" PRIu32 " seconds", timeoutSeconds);
#if ESP_IDF_VERSION_MAJOR < 5
  return esp_task_wdt_init(timeoutSeconds, panic) == ESP_OK;
#else
  esp_task_wdt_config_t config = {
    .timeout_ms = timeoutSeconds * 1000,
    .idle_core_mask = (1 << SOC_CPU_CORES_NUM) - 1, // Bitmask of all cores
    .trigger_panic = panic,
  };
  return esp_task_wdt_init(&config) == ESP_OK || esp_task_wdt_reconfigure(&config) == ESP_OK;
#endif
}

#ifdef MYCILA_JSON_SUPPORT
void Mycila::TaskManager::toJson(const JsonObject& root) const {
  const State s = state();
  root["name"] = _name;
  root["state"] = s == State::RUNNING ? "RUNNING" : (s == State::PAUSING ? "PAUSING" : "PAUSED");
  #ifndef MYCILA_TASK_MANAGER_NO_STATS
  if (_stats && _stats->bins() && _stats->count())
    _stats->toJson(root["stats"].to<JsonObject>());
  #endif
  for (auto& task : _tasks)
    task->toJson(root["tasks"].add<JsonObject>());
}
#endif
