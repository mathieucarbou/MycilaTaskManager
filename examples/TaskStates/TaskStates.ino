// SPDX-License-Identifier: MIT
/*
 * Copyright (C) Mathieu Carbou
 *
 * Shows what a single Task can do on its own, without a manager, and how to read its
 * execution state (v5): IDLE, RUNNING (the task function only) and FINALIZING
 * (the function returned, statistics and onDone are still running).
 *
 * Serial output shows:
 *  - the difference between running() and active() while onDone executes
 *  - tryRun() honouring the interval, forceRun() bypassing everything
 *  - requestEarlyRun() ignoring the interval once
 *  - remainingTime() counting down to the next run
 */
#include <Arduino.h>
#include <MycilaTaskManager.h>

Mycila::Task led("led", Mycila::Task::Type::FOREVER, [](void*) {
  // in a real sketch this would blink an LED
  Serial.println("[led] blink");
});

void observe(const char* when) {
  Serial.printf("[observe] %s: state=%d running=%d active=%d remaining=%lu paused=%d\n",
                when,
                (int)led.state(),
                (int)led.running(),
                (int)led.active(),
                (unsigned long)led.remainingTime(),
                (int)led.paused());
}

void setup() {
  Serial.begin(115200);
  while (!Serial)
    continue;

  // 1. a task with an interval is not due yet: tryRun() honours the interval
  led.setInterval(2000);
  observe("interval set to 2000 ms");
  if (!led.tryRun())
    Serial.println("[tryRun] not due yet, skipped (2000 ms interval)");

  // 2. requestEarlyRun() ignores the interval for the next run only
  led.requestEarlyRun();
  if (led.tryRun())
    Serial.println("[tryRun] early run requested: ran immediately");
  observe("after the early run");

  // 3. forceRun() runs the task now, ignoring the interval AND the paused state
  led.pause();
  led.forceRun();
  Serial.println("[forceRun] ran although the task is paused");
  observe("after forceRun");

  // 4. running() vs active(): the task function returned, but onDone is still running.
  //    while onDone executes, the state is FINALIZING: running() is false, active() is true.
  led.resume();
  led.onDone([](const Mycila::Task& me, uint32_t elapsed) {
    Serial.printf("[onDone] elapsed=%lu ms, state=%d (FINALIZING), running()=%d, active()=%d\n",
                  (unsigned long)elapsed,
                  (int)me.state(),
                  (int)me.running(),
                  (int)me.active());
    delay(50); // simulate work done in the callback
  });
  led.forceRun();
  observe("after forceRun with onDone: the callback is done, back to IDLE");
}

void loop() {
  vTaskDelay(portMAX_DELAY);
}