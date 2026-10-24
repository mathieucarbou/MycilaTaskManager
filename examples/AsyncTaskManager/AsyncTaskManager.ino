// SPDX-License-Identifier: MIT
/*
 * Copyright (C) Mathieu Carbou
 *
 * A manager running in its own FreeRTOS task: the Arduino loop() is free.
 * Tasks chained through onDone still run on the worker, and the manager
 * keeps running while loop() does other work.
 *
 * Each property prints PASS/FAIL and the sketch ends with a summary, so the
 * output can be verified automatically.
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

Mycila::TaskManager bgManager("bg");

Mycila::Task tick("tick", Mycila::Task::Type::FOREVER, [](void*) { /* no print: counted below */ });

uint32_t tickRuns = 0;
uint32_t chainedRuns = 0;
Mycila::Task chained("chained", Mycila::Task::Type::ONCE, [](void*) { /* counted below */ });

void setup() {
  Serial.begin(115200);
  while (!Serial)
    continue;
  Serial.println("\n== AsyncTaskManager example ==");

  tick.setInterval(500);
  tick.onDone([](const Mycila::Task&, uint32_t) {
    tickRuns++;
    // chain another task from the callback: it will be dispatched by the worker
    chained.resume();
  });
  bgManager.addTask(tick);

  chained.onDone([](const Mycila::Task&, uint32_t) { chainedRuns++; });
  bgManager.addTask(chained);

  // start the manager in its own FreeRTOS task; loop() stays free
  check(bgManager.asyncStart(4096, -1, -1, 10, true), "asyncStart() created the worker task");
  check(bgManager.state() == Mycila::TaskManager::State::RUNNING, "manager is RUNNING");

  check(!tick.paused(), "interval task is running on the worker");
  check(chained.paused(), "chained ONCE task starts paused");
}

void loop() {
  // loop() is free: verify the async behaviour from here, every 500 ms
  static uint32_t lastCheck = 0;
  if (millis() - lastCheck < 500)
    return;
  lastCheck = millis();

  static int checks = 0;
  checks++;
  if (checks == 6) {
    // ~3 s: the worker dispatched the interval task about 6 times
    check(tickRuns >= 4 && tickRuns <= 8, "tick ran ~6 times in 3 s on the worker");
    check(chainedRuns == tickRuns, "every tick chained exactly one ONCE run");
  } else if (checks == 12) {
    // ~6 s: still running, the manager never paused
    check(tickRuns >= 10, "tick kept running at 6 s");
    check(bgManager.state() == Mycila::TaskManager::State::RUNNING, "manager never left RUNNING");

    // the worker is a real FreeRTOS task with our name
    check(xTaskGetHandle("bg") != NULL, "worker task 'bg' exists");
  } else if (checks == 14) {
    bgManager.asyncStop();
    Serial.printf("== RESULT: %d passed, %d failed ==\n", g_pass, g_fail);
    Serial.println("== AsyncTaskManager example complete ==");
    vTaskDelete(NULL);
  }
}