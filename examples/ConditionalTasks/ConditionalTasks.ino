// SPDX-License-Identifier: MIT
/*
 * Copyright (C) Mathieu Carbou
 *
 * Dynamic task management and conditional execution:
 *  - newTask(): the manager owns the task (no global instance)
 *  - setEnabledWhen(): a predicate evaluated on every pass
 *  - pauseTasks()/resumeTasks(): the v5 names of the v4 pause()/resume() broadcast
 *  - waitForAllTasksToComplete(): quiesce before touching shared state
 *  - a task can pause itself from inside its own execution
 *
 * Verified properties, each printed as PASS/FAIL. The manager runs async,
 * and the verification runs from loop() (a context outside the worker).
 */
#include <Arduino.h>
#include <MycilaTaskManager.h>

static int g_pass = 0;
static int g_fail = 0;

static void check(bool cond, const char* what) {
  Serial.printf("%s: %s\n", cond ? "PASS" : "FAIL", what);
  if (cond)
    g_pass++;
  else
    g_fail++;
}

Mycila::TaskManager manager("conditional");

volatile bool linkUp = false;
int samples = 0;

Mycila::Task* watchPtr = nullptr;

void setup() {
  Serial.begin(115200);
  while (!Serial)
    continue;
  Serial.println("\n== ConditionalTasks example ==");

  // 1. newTask(): manager-owned tasks, no globals
  Mycila::Task& heartbeat = manager.newTask("heartbeat", [](void*) { /* counted below */ });
  heartbeat.setInterval(100);

  // 2. setEnabledWhen(): scheduled() is false while the predicate is false
  Mycila::Task& sensor = manager.newTask("sensor", [](void*) { samples++; });
  sensor.setInterval(100);
  sensor.setEnabledWhen([]() { return linkUp; });

  Mycila::Task& control = manager.newTask("control", Mycila::Task::Type::ONCE, [](void*) {
    linkUp = true; // the sensor starts running
  });
  control.resume(300);

  // 3. the orchestrator: pauses itself after exercising the broadcasts
  Mycila::Task& watch = manager.newTask("watch", Mycila::Task::Type::FOREVER, [](void*) {
    static uint32_t lastChange = 0;
    if (millis() - lastChange < 1000)
      return;
    lastChange = millis();

    if (samples >= 5) {
      // v5 per-task broadcast: the manager keeps dispatching, tasks are skipped
      manager.pauseTasks();
      // quiesce from inside the worker is safe: it returns the current state
      if (manager.waitForAllTasksToComplete(0))
        watchPtr->pause(); // stop this task itself
    } else if (linkUp) {
      linkUp = false; // the sensor stops being scheduled, but stays registered
    } else {
      linkUp = true;
    }
  });
  watch.setInterval(200);
  watchPtr = &watch;

  check(manager.tasks() == 4, "4 manager-owned tasks created");
  check(!sensor.scheduled(), "sensor is not scheduled while the link is down");
  sensor.setEnabledWhen([]() { return linkUp; }); // re-install the same predicate: no effect on state
  check(!sensor.scheduled(), "re-installing the predicate did not un-schedule it");
  check(!control.paused(), "resume(300) un-paused the control task, it waits for its delay");
  check(control.interval() == 300, "resume(300) re-based the control interval to 300 ms");
  check(manager.state() == Mycila::TaskManager::State::RUNNING, "manager is RUNNING");

  manager.asyncStart(4096, -1, -1, 10, true);
}

void loop() {
  // verification from outside the worker, every 500 ms
  static uint32_t lastCheck = 0;
  if (millis() - lastCheck < 500)
    return;
  lastCheck = millis();

  static int checks = 0;
  checks++;
  if (checks == 2) {
    // ~1 s: control ran, the link went up, the sensor samples.
    // (linkUp itself may already have been flipped back by the watch task: the
    //  samples count is the race-free proof that control fired)
    check(samples >= 1, "control fired: the sensor started sampling");
  } else if (checks == 4) {
    // ~2 s: the sensor runs while the link is up, then the watch flips it down
    check(samples >= 2, "sensor kept sampling while the link was up");
  } else if (checks == 12) {
    // ~6 s: after several link toggles, samples >= 5 triggered the broadcast demo
    check(samples >= 5, "at least 5 samples were produced");
    check(watchPtr->paused(), "the watch task paused itself (per-task pause from inside)");
    // the manager is still RUNNING: pauseTasks() never touched the gate
    check(manager.state() == Mycila::TaskManager::State::RUNNING, "the manager gate was never closed");
    Serial.printf("== RESULT: %d passed, %d failed ==\n", g_pass, g_fail);
    Serial.println("== ConditionalTasks example complete ==");
    vTaskDelete(NULL);
  }
}