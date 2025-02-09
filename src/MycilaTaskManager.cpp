// SPDX-License-Identifier: MIT
/*
 * Copyright (C) 2023-2024 Mathieu Carbou
 */
#include <MycilaTaskManager.h>

#include <esp32-hal-log.h>
#include <esp_timer.h>

#include <string>

#ifdef MYCILA_LOGGER_SUPPORT
  #include <MycilaLogger.h>
extern Mycila::Logger logger;
  #define LOGD(tag, format, ...) logger.debug(tag, format, ##__VA_ARGS__)
  #define LOGI(tag, format, ...) logger.info(tag, format, ##__VA_ARGS__)
  #define LOGW(tag, format, ...) logger.warn(tag, format, ##__VA_ARGS__)
  #define LOGE(tag, format, ...) logger.error(tag, format, ##__VA_ARGS__)
#else
  #define LOGD(tag, format, ...) ESP_LOGD(tag, format, ##__VA_ARGS__)
  #define LOGI(tag, format, ...) ESP_LOGI(tag, format, ##__VA_ARGS__)
  #define LOGW(tag, format, ...) ESP_LOGW(tag, format, ##__VA_ARGS__)
  #define LOGE(tag, format, ...) ESP_LOGE(tag, format, ##__VA_ARGS__)
#endif

#define TAG "TASKS"

static bool PREDICATE_FALSE() { return false; };

void Mycila::TaskManager::log() {
  if (_stats) {
    uint8_t binCount = _stats->bins();
    if (binCount) {
      std::string line;
      line.reserve(192);
      for (uint8_t i = 0; i < binCount; i++) {
        line += " | ";
        line += std::to_string(_stats->bin(i));
        line += (i < binCount - 1 ? " < 2^" : " >= 2^");
        line += std::to_string(i < binCount - 1 ? (i + 1) : i);
      }
      line += " |";
      LOGI(TAG, "| %30s%s count=%" PRIu32, _name, line.c_str(), _stats->count());
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
  if (_enabled) {
    _enabled = nullptr;
  } else {
    _enabled = PREDICATE_FALSE;
  }
  return *this;
}

Mycila::Task& Mycila::Task::log() {
  if (!_stats)
    return *this;

  uint8_t binCount = _stats->bins();
  if (!binCount)
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
  LOGI(TAG, "| %30s%s count=%" PRIu32, _name, line.c_str(), _stats->count());
  return *this;
}
