// SPDX-License-Identifier: MIT
/*
 * Copyright (C) 2023-2025 Mathieu Carbou
 */
#include <MycilaTaskManager.h>

#include <esp32-hal-log.h>
#include <esp_timer.h>

#include <string>

#define TAG "TASKS"

static bool PREDICATE_FALSE() { return false; };

void Mycila::TaskManager::log() {
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
      LOGI(TAG, "| %30s%s count=%" PRIu32, _name, line.c_str(), count);
    }
  }
  for (auto& task : _tasks)
    task->log();
}

bool Mycila::TaskManager::asyncStart(uint32_t stackSize, BaseType_t priority, BaseType_t coreID, uint32_t delay, bool wdt) {
  if (_taskManagerHandle)
    return false;

  _delay = delay;

  if (coreID < 0)
    coreID = xPortGetCoreID();

  UBaseType_t prio = priority < 0 ? uxTaskPriorityGet(NULL) : priority;
  bool b = xTaskCreateUniversal(_asyncTaskManager, _name, stackSize, this, prio, &_taskManagerHandle, coreID) == pdPASS;

  if (b) {
    LOGD(TAG, "Task manager '%s' started async: core: %d, priority: %" PRIu32 ", stack: %" PRIu32 ", handle: %p", _name, coreID, prio, stackSize, _taskManagerHandle);

    if (wdt && esp_task_wdt_add(_taskManagerHandle) == ESP_OK) {
      LOGD(TAG, "Task manager '%s' added to WDT", _name);
    }
    _wdt = wdt;
  }
  return b;
}

void Mycila::TaskManager::asyncStop() {
  if (!_taskManagerHandle)
    return;
  LOGD(TAG, "Stopping async task manager with handle: %p", _taskManagerHandle);
  esp_task_wdt_delete(_taskManagerHandle);
  vTaskDelete(_taskManagerHandle);
  _taskManagerHandle = NULL;
}

bool Mycila::TaskManager::configureWDT(uint32_t timeoutSeconds, bool panic) {
  LOGI(TAG, "Configuring Task Watchdog Timer (TWDT) to %" PRIu32 " seconds", timeoutSeconds);
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

Mycila::Task::~Task() {
  if (_stats)
    delete _stats;
}

Mycila::Task& Mycila::Task::setEnabled(bool enabled) {
  if (enabled) {
    _enabled = nullptr;
  } else {
    _enabled = PREDICATE_FALSE;
  }
  return *this;
}

Mycila::Task& Mycila::Task::log() {
  if (!_stats)
    return *this;

  const uint8_t binCount = _stats->bins();
  const uint32_t count = _stats->count();

  if (!binCount || !count)
    return *this;

  std::string line;
  line.reserve(192);
  for (uint8_t i = 0; i < binCount; i++) {
    line += " | ";
    line += std::to_string(_stats->bin(i));
    line += (i < binCount - 1 ? " < 2^" : " >= 2^");
    line += std::to_string(i < binCount - 1 ? (i + 1) : i);
  }
  line += " |";
  LOGI(TAG, "| %30s%s count=%" PRIu32, _name, line.c_str(), count);
  return *this;
}
