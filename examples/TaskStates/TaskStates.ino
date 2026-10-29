// SPDX-License-Identifier: MIT
/*
 * Copyright (C) Mathieu Carbou
 *
 * A task driven by hand, without a manager, and the v5 execution state:
 * IDLE, RUNNING (the task function only) and FINALIZING (the function
 * returned, statistics and onDone are still running).
 *
 * Verified properties, each printed as PASS/FAIL:
 *  - tryRun() honours the interval
 *  - requestEarlyRun() ignores the interval once
 *  - forceRun() bypasses the interval AND the paused state
 *  - running() is false while onDone runs, active() is true
 *  - a ONCE task re-arming itself from its function body is preserved (the
 *    pause happens at run start, so resume() during the run is not lost)
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

uint32_t fnRuns = 0;
uint32_t doneRuns = 0;
bool sawFinalizingWhileDone = false;

Mycila::Task task("task", Mycila::Task::Type::FOREVER, [](void*) {
  fnRuns++;
  Serial.println("[task] function running");
});

void setup() {
  Serial.begin(115200);
  while (!Serial)
    continue;
  Serial.println("\n== TaskStates example ==");

  task.onDone([](const Mycila::Task& me, uint32_t) {
    doneRuns++;
    // while this callback runs, the state is FINALIZING: running() false, active() true
    if (me.state() == Mycila::Task::State::FINALIZING && !me.running() && me.active())
      sawFinalizingWhileDone = true;
    Serial.println("[task] onDone running");
  });

  // 1. a task that never ran is due immediately, even with an interval:
  //    lastEndTime() == 0 means "due now" (see shouldRun())
  task.setInterval(5000);
  check(task.lastEndTime() == 0, "the task has never run, lastEndTime() is 0");
  check(task.shouldRun(), "never-run task is due immediately");
  check(task.tryRun(), "tryRun() ran the never-run task");
  check(fnRuns == 1, "the function ran exactly once");
  check(sawFinalizingWhileDone, "onDone observed FINALIZING: running() false, active() true");
  check(doneRuns == 1, "onDone ran once");
  check(task.state() == Mycila::Task::State::IDLE, "back to IDLE after onDone");

  // 2. the interval clock was re-based by the run: not due yet
  check(!task.shouldRun(), "not due: the interval restarted after the run");
  check(!task.tryRun(), "tryRun() now honours the 5 s interval");
  check(task.remainingTime() > 4000, "remaining time is close to 5 s");

  // 3. requestEarlyRun() ignores the interval, once
  task.requestEarlyRun();
  check(task.earlyRunRequested(), "early run requested");
  check(task.tryRun(), "tryRun() ran after requestEarlyRun()");
  check(fnRuns == 2, "the function ran a second time");
  check(doneRuns == 2, "onDone ran a second time");

  // 4. forceRun() bypasses the interval AND the paused state
  task.pause();
  check(task.paused(), "task paused");
  check(!task.shouldRun(), "paused task is not scheduled");
  task.forceRun();
  check(fnRuns == 3, "forceRun() ran although the task is paused");
  check(task.paused(), "the task is still paused after forceRun()");

  // 5. tryRun() on a paused task does nothing
  check(!task.tryRun(), "tryRun() skipped a paused task");
  check(fnRuns == 3, "no additional run");

  // 6. a ONCE task re-arming itself from its function body: the pause now happens
  //    at run start, so the resume() executed by the function is not overwritten
  //    when the run finishes, and the task executes once more
  uint32_t onceRuns = 0;
  Mycila::Task once("once", Mycila::Task::Type::ONCE, [&](void*) {
    onceRuns++;
    if (onceRuns == 1) {
      check(once.paused(), "the ONCE task is paused while its function runs (pause at run start)");
      once.resume(); // from inside the function: must survive the self-pause
    }
  });
  once.resume();
  check(once.tryRun(), "first ONCE run started");
  check(onceRuns == 1, "the ONCE function ran once");
  check(!once.paused(), "the in-run resume() was preserved, not overwritten at run end");
  check(once.tryRun(), "tryRun() ran the ONCE task a second time");
  check(onceRuns == 2, "the ONCE task executed once more");
  check(!once.tryRun(), "without a new resume() the ONCE task stays paused");
  check(onceRuns == 2, "no further run without a new resume()");

  Serial.printf("== RESULT: %d passed, %d failed ==\n", g_pass, g_fail);
  Serial.println("== TaskStates example complete ==");
}

void loop() {
  vTaskDelay(portMAX_DELAY);
}