// SPDX-License-Identifier: MIT
/*
 * Copyright (C) Mathieu Carbou
 *
 * Synchronous manager driven from loop(): interval scheduling, a ONCE task
 * re-armed from an onDone callback, a delayed ONCE task, and profiling.
 *
 * Each property prints PASS/FAIL and the sketch ends with a summary, so the
 * output can be verified automatically (see tools/verify-example.sh).
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

Mycila::TaskManager loopTaskManager("loop()");

Mycila::Task sayHello("sayHello", [](void* params) { Serial.println("Hello"); });
Mycila::Task ping("ping", Mycila::Task::Type::ONCE, [](void* params) { Serial.println("Pong"); });
Mycila::Task delayed("delayed", Mycila::Task::Type::ONCE, [](void* params) { Serial.println("Delayed!"); });

uint32_t pingRuns = 0;
uint32_t delayedRuns = 0;
uint32_t helloRuns = 0;

char* params = "Pong";

void setup() {
  Serial.begin(115200);
  while (!Serial)
    continue;
  Serial.println("\n== TaskManager example ==");

  // 1. interval scheduling: the task must run about every 1000 ms
  sayHello.setInterval(1000);
  loopTaskManager.addTask(sayHello);

  // 2. a ONCE task re-armed from a callback of another task
  ping.onDone([](const Mycila::Task&, uint32_t) { pingRuns++; });
  loopTaskManager.addTask(ping);

  // 3. a delayed ONCE task: resume(2000) re-bases the interval clock,
  //    so the task runs once, about 2 s from now
  delayed.onDone([](const Mycila::Task&, uint32_t) { delayedRuns++; });
  delayed.resume(2000);
  loopTaskManager.addTask(delayed);

  // 4. arm the ping from sayHello's onDone, like a producer scheduling work
  sayHello.onDone([](const Mycila::Task& me, uint32_t) {
    helloRuns++;
    ping.resume();
  });

  check(sayHello.shouldRun(), "interval task is due immediately (never ran)");
  check(ping.paused(), "ONCE task starts paused");
  check(!delayed.paused(), "resume(2000) un-paused the delayed task, it now waits for its delay");
  check(delayed.interval() == 2000, "resume(2000) re-based the interval to 2000 ms");
  check(!loopTaskManager.empty(), "manager has 3 tasks registered");
  check(loopTaskManager.tasks() == 3, "manager reports 3 tasks");
}

void loop() {
  // run the manager, then verify the expected behaviour every 500 ms
  static uint32_t lastCheck = 0;
  loopTaskManager.loop(10);

  if (millis() - lastCheck < 500)
    return;
  lastCheck = millis();

  static int checks = 0;
  checks++;
  if (checks == 5) {
    // ~2.5 s: hello ran about 2-3 times, the delayed task fired exactly once
    check(helloRuns >= 2 && helloRuns <= 4, "interval task ran ~2-3 times in 2.5 s");
    check(delayedRuns == 1, "delayed task fired exactly once after its 2 s delay");
    check(pingRuns >= 1, "ping was re-armed and ran at least once");
  } else if (checks == 10) {
    // ~5 s: the delayed task fired exactly once
    check(delayedRuns == 1, "delayed ONCE task fired exactly once after 2 s");
    check(delayed.paused(), "delayed task paused itself after firing (ONCE)");
    check(pingRuns >= 3, "ping was re-armed by the producer at each hello");
  } else if (checks == 13) {
    // summary and exit: no more scheduling
    Serial.printf("== RESULT: %d passed, %d failed ==\n", g_pass, g_fail);
    Serial.println("== TaskManager example complete ==");
    vTaskDelete(NULL);
  }
}