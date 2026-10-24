// SPDX-License-Identifier: MIT
/*
 * Copyright (C) Mathieu Carbou
 *
 * Shows dynamic task management and conditional execution:
 *  - newTask(): the manager owns the task (no global instance to declare)
 *  - setEnabledWhen(): run a task only while a predicate is true
 *  - pauseTasks()/resumeTasks(): the v5 names of the v4 pause()/resume() broadcast
 *  - waitForAllTasksToComplete(): quiesce before touching something the tasks use
 *  - onDone(): a callback can pause its own task
 *
 * The "sensor" task only runs while the simulated link is up, then the per-task
 * broadcast is exercised once it has produced enough samples.
 */
#include <Arduino.h>
#include <MycilaTaskManager.h>

Mycila::TaskManager manager("conditional");

// simulated link state, toggled by the control task
volatile bool linkUp = false;
int samples = 0;

// the orchestrator pauses itself when the demo is over
Mycila::Task* watchPtr = nullptr;

void setup() {
  Serial.begin(115200);
  while (!Serial)
    continue;

  // 1. newTask(): the manager owns the task, no global instance needed
  Mycila::Task& heartbeat = manager.newTask("heartbeat", [](void*) {
    Serial.println("[heartbeat] alive");
  });
  heartbeat.setInterval(1000);

  // 2. setEnabledWhen(): scheduled() is false while the predicate returns false,
  //    so the manager skips it on every pass without touching the task
  Mycila::Task& sensor = manager.newTask("sensor", [](void*) {
    samples++;
    Serial.printf("[sensor] sample %d\n", samples);
  });
  sensor.setInterval(200);
  sensor.setEnabledWhen([]() { return linkUp; });

  // 3. a control task: brings the link up, so the sensor starts running
  Mycila::Task& control = manager.newTask("control", Mycila::Task::Type::ONCE, [](void*) {
    linkUp = true;
  });
  control.resume(500); // run once, 500 ms from now

  // 4. the orchestrator: after enough samples, exercises the v5 per-task broadcasts
  Mycila::Task& watch = manager.newTask("watch", Mycila::Task::Type::FOREVER, [](void*) {
    static uint32_t lastChange = 0;
    if (millis() - lastChange < 2000)
      return;
    lastChange = millis();

    if (samples >= 5) {
      // the v5 name of the v4 manager broadcast: pause every task.
      // the manager keeps dispatching; every task is simply skipped.
      manager.pauseTasks();
      Serial.println("[watch] every task paused (pauseTasks())");

      // quiesce before touching something the tasks use.
      // 0 = wait forever; a value would be a deadline returning false on timeout.
      if (manager.waitForAllTasksToComplete(0))
        Serial.println("[watch] all tasks idle, safe to touch shared state");

      // resume the broadcast: every task runs again on its next due time
      manager.resumeTasks();
      Serial.println("[watch] every task resumed (resumeTasks())");

      // done: this task stops itself. pause() from inside a task is safe: the
      // current execution finishes normally, the task simply won't run again.
      watchPtr->pause();
      Serial.println("[watch] watch task paused itself, demo complete");
    } else if (linkUp) {
      linkUp = false; // the sensor stops running, but stays registered
      Serial.println("[watch] link down, sensor idle but still scheduled");
    } else {
      linkUp = true;
      Serial.println("[watch] link up, sensor running again");
    }
  });
  watch.setInterval(500);
  watchPtr = &watch;

  manager.asyncStart(4096, -1, -1, 10, true);
  Serial.println("running: the sensor follows the link, then pauseTasks()/resumeTasks() fire");
}

void loop() {
  vTaskDelay(portMAX_DELAY);
}