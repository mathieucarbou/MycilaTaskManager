// SPDX-License-Identifier: MIT
/*
 * Copyright (C) Mathieu Carbou
 */
#include <MycilaTaskManager.h>

#include <esp32-hal-log.h>

#include <string>

#define TAG "TASKS"

static constexpr bool PREDICATE_FALSE() { return false; };

Mycila::Task::~Task() {
#ifndef MYCILA_TASK_MANAGER_NO_STATS
  if (_stats)
    delete _stats;
#endif
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
#ifndef MYCILA_TASK_MANAGER_NO_STATS
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
  ESP_LOGI(TAG, "| %30s%s count=%" PRIu32, _name, line.c_str(), count);
#endif
  return *this;
}

#ifdef MYCILA_JSON_SUPPORT
void Mycila::Task::toJson(const JsonObject& root) const {
  root["name"] = _name;
  root["type"] = _type == Type::ONCE ? "ONCE" : "FOREVER";
  root["paused"] = _paused;
  root["enabled"] = enabled();
  root["interval"] = _intervalMs;
  root["state"] = _state == State::RUNNING ? "RUNNING" : (_state == State::FINALIZING ? "FINALIZING" : "IDLE");
  #ifndef MYCILA_TASK_MANAGER_NO_STATS
  if (_stats && _stats->bins() && _stats->count())
    _stats->toJson(root["stats"].to<JsonObject>());
  #endif
}
#endif
