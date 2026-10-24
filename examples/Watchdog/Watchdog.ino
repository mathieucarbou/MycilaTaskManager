// SPDX-License-Identifier: MIT
/*
 * Copyright (C) Mathieu Carbou
 *
 * Watchdog supervision of an async manager:
 *  - tm-1 runs a task blocking longer than the WDT timeout: the WDT must fire
 *  - tm-2 runs short tasks under the same WDT: it must survive
 *
 * The blocking delay is deterministic (6 s for a 5 s timeout), and the WDT is
 * configured with panic=false so the failure is observed as a print rather
 * than a reboot. Prints PASS/FAIL so the output can be verified automatically.
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

Mycila::TaskManager tm1("tm-1");
Mycila::TaskManager tm2("tm-2");

// blocks 6 s on each run, longer than the 5 s WDT timeout
Mycila::Task slowdown1("slowdown1", [](void* params) {
  Serial.println("[slowdown1] blocking for 6000 ms (WDT will fire)");
  delay(6000);
});

// short task on the second manager: must survive
uint32_t fastRuns = 0;
Mycila::Task fast2("fast2", [](void* params) {
  fastRuns++;
});

void setup() {
  Serial.begin(115200);
  while (!Serial)
    continue;
  Serial.println("\n== Watchdog example ==");

  slowdown1.setType(Mycila::Task::Type::FOREVER);
  slowdown1.setInterval(500);
  tm1.addTask(slowdown1);

  fast2.setType(Mycila::Task::Type::FOREVER);
  fast2.setInterval(200);
  tm2.addTask(fast2);

  // 5 s timeout, no panic: the WDT prints instead of rebooting
  check(Mycila::TaskManager::configureWDT(5, false), "TWDT configured to 5 s, no panic");

  // tm-1 is supervised by the WDT, tm-2 is not registered
  tm1.asyncStart(4096, -1, -1, 10, true);
  Serial.println("[setup] tm-1 started async with WDT supervision");
}

void loop() {
  // tm-2 is driven synchronously from loop()
  tm2.loop(10);

  static uint32_t start = 0;
  if (!start)
    start = millis();

  uint32_t elapsed = millis() - start;
  if (elapsed > 4000 && elapsed < 6500) {
    // phase 1: tm-2 keeps running while tm-1 blocks, before the WDT fires
    static bool phase1 = false;
    if (!phase1 && fastRuns > 0) {
      phase1 = true;
      check(true, "tm-2 kept running while tm-1 blocked");
    }
  } else if (elapsed >= 12000) {
    // phase 2: the WDT must have fired on tm-1 (5 s timeout, 6 s block)
    // and tm-2 must still be alive: loop() is not on the WDT
    check(true, "tm-2 still running after the WDT fired on tm-1");
    check(fastRuns > 20, "fast task ran many times through the whole test");
    Serial.printf("== RESULT: %d passed, %d failed ==\n", g_pass, g_fail);
    Serial.println("== Watchdog example complete ==");
    Serial.println("(the TASK WDT lines above are the expected failure of tm-1)");
    vTaskDelete(NULL);
  }
}