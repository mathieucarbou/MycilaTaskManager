// SPDX-License-Identifier: MIT
/*
 * Copyright (C) Mathieu Carbou
 *
 * Demonstrates the use case from issue #18: stop the async worker, wait for a confirmed pause,
 * perform an operation requiring exclusive access to a shared resource, then continue.
 *
 * Two things are shown:
 *  - a producer task whose onDone re-arms a follow-up task: the follow-up cannot run while the
 *    manager is confirmed paused, because the pause gate is checked between two executions
 *  - a completed ONCE task keeps its state across pause()/resume(): resume() does not re-arm it
 */
#include <Arduino.h>
#include <MycilaTaskManager.h>

Mycila::TaskManager manager("bg");

// The shared resource the tasks and the maintenance operation both need.
portMUX_TYPE resourceMux = portMUX_INITIALIZER_UNLOCKED;
int resourceUsers = 0;

// Re-armed by the producer's onDone, so the producer schedules work *after* a pause is requested.
Mycila::Task followUp("followUp", Mycila::Task::Type::ONCE, [](void*) {
  portENTER_CRITICAL(&resourceMux);
  resourceUsers++;
  portEXIT_CRITICAL(&resourceMux);
  Serial.println("[followUp] used the shared resource");
});

Mycila::Task producer("producer", [](void*) {
  Serial.println("[producer] working");
});

// Completes and pauses itself: it must stay completed after a manager resume().
Mycila::Task initTask("init", Mycila::Task::Type::ONCE, [](void*) {
  Serial.println("[init] initialization done");
});

// A maintenance request raised from an unrelated task, as an OTA or reset would.
volatile bool maintenanceRequested = false;

void maintenanceTask(void*) {
  for (;;) {
    if (maintenanceRequested) {
      maintenanceRequested = false;

      // 1. request the pause: non-blocking, does not touch any task
      manager.pause();

      // 2. wait for the confirmation: only PAUSED guarantees no callback is active
      if (!manager.waitForPaused(5000)) {
        Serial.println("[maintenance] pause not confirmed, aborting (no protected work)");
        manager.resume(); // cancel the request
        vTaskDelay(pdMS_TO_TICKS(1000));
        continue;
      }

      // 3. exclusive access: no task of this manager is running, onDone included,
      //    and none will start until resume()
      portENTER_CRITICAL(&resourceMux);
      int users = resourceUsers;
      portEXIT_CRITICAL(&resourceMux);
      Serial.printf("[maintenance] protected work, resource used %d time(s)\n", users);
      Serial.printf("[maintenance] init task paused=%d (must stay 1)\n", initTask.paused());

      // 4. continue: task states, intervals and timestamps were never modified
      manager.resume();
      Serial.println("[maintenance] resumed");
    }
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

void setup() {
  Serial.begin(115200);
  while (!Serial)
    continue;

  producer.setInterval(300);
  producer.onDone([](const Mycila::Task&, uint32_t) {
    // schedules more work from a callback: a plain task resume cannot bypass the manager gate
    followUp.resume();
  });
  manager.addTask(producer);

  initTask.resume(); // run once on the next dispatch
  manager.addTask(initTask);

  followUp.setEnabled(true);
  manager.addTask(followUp);

  manager.asyncStart(4096, -1, -1, 10, true);

  xTaskCreateUniversal(maintenanceTask, "maintenance", 4096, nullptr, 1, nullptr, ARDUINO_RUNNING_CORE);

  Serial.println("Type 'm' to request a maintenance pause");
}

void loop() {
  if (Serial.available() && Serial.read() == 'm')
    maintenanceRequested = true;
  vTaskDelay(pdMS_TO_TICKS(50));
}
