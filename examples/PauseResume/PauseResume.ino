// SPDX-License-Identifier: MIT
/*
 * Copyright (C) Mathieu Carbou
 *
 * The issue #18 scenario: quiesce an async manager before exclusive access
 * to a shared resource, then continue without disturbing task state.
 *
 * Verified properties, each printed as PASS/FAIL:
 *  - a producer whose onDone re-arms a follow-up task cannot make it run
 *    while the manager is paused (the gate, not the task, decides)
 *  - a completed ONCE task stays completed across pause()/resume()
 *  - only a confirmed pause (state() == PAUSED) means no callback is active
 *  - resume() reopens the gate and the tasks continue
 *
 * The maintenance runs from a separate FreeRTOS task, as an OTA would.
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

Mycila::TaskManager manager("bg");

uint32_t producerRuns = 0;
uint32_t followUpRuns = 0;
uint32_t initRuns = 0;

// re-armed by the producer's onDone, so the producer schedules work
Mycila::Task followUp("followUp", Mycila::Task::Type::ONCE, [](void*) { followUpRuns++; });

Mycila::Task producer("producer", [](void*) { producerRuns++; });

// completes and pauses itself: it must stay completed across pause()/resume()
Mycila::Task initTask("init", Mycila::Task::Type::ONCE, [](void*) { initRuns++; });

volatile int maintenancePhase = 0;

void maintenanceTask(void*) {
  for (;;) {
    if (maintenancePhase == 1) {
      maintenancePhase = 2;

      // request the pause: non-blocking, does not touch any task
      manager.pause();
      check(manager.state() == Mycila::TaskManager::State::PAUSING ||
                manager.state() == Mycila::TaskManager::State::PAUSED,
            "pause() moved the manager out of RUNNING");

      // wait for the confirmation: only PAUSED guarantees no callback is active
      if (!manager.waitForPaused(5000)) {
        check(false, "pause confirmed within 5 s");
        manager.resume();
        maintenancePhase = 4;
        continue;
      }
      check(true, "pause confirmed within 5 s");

      // exclusive access: no task of this manager is running, onDone included
      check(manager.paused(), "manager reports paused");
      uint32_t followDuringPause = followUpRuns;
      delay(100); // hold the pause: if the gate leaked, the worker would run tasks
      check(followUpRuns == followDuringPause, "re-armed followUp did not run while paused");
      check(initTask.paused(), "completed ONCE task stayed paused during the manager pause");
      check(producerRuns > 0, "producer did run before the pause");

      manager.resume();
      maintenancePhase = 3;
    } else if (maintenancePhase == 3) {
      // after resume: task state was never modified
      check(initRuns == 1, "init ran exactly once (never re-armed by resume)");
      delay(500);
      maintenancePhase = 4;
    }
    delay(10);
  }
}

void setup() {
  Serial.begin(115200);
  while (!Serial)
    continue;
  Serial.println("\n== PauseResume example ==");

  producer.setInterval(300);
  producer.onDone([](const Mycila::Task&, uint32_t) {
    followUp.resume(); // schedules more work from a callback: cannot bypass the gate
  });
  manager.addTask(producer);

  initTask.resume(); // run once on the next dispatch
  manager.addTask(initTask);

  followUp.setEnabled(true);
  manager.addTask(followUp);

  manager.asyncStart(4096, -1, -1, 10, true);

  xTaskCreateUniversal(maintenanceTask, "maintenance", 4096, nullptr, 1, nullptr, ARDUINO_RUNNING_CORE);

  // let the producer run a few times, then request the maintenance pause
  delay(1500);
  maintenancePhase = 1;
}

void loop() {
  if (maintenancePhase == 4) {
    delay(600); // give the worker time to dispatch the re-armed followUp
    check(followUpRuns > 0, "followUp ran after resume() (the producer kept re-arming it)");
    Serial.printf("== RESULT: %d passed, %d failed ==\n", g_pass, g_fail);
    Serial.println("== PauseResume example complete ==");
    vTaskDelete(NULL);
  }
  vTaskDelay(pdMS_TO_TICKS(10));
}