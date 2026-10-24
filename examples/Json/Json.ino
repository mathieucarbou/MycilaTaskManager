// SPDX-License-Identifier: MIT
/*
 * Copyright (C) Mathieu Carbou
 *
 * JSON export of the task tree: the manager state (v5) and each task state,
 * alongside the classic profiling log.
 *
 * The sketch serializes the manager, parses the JSON back and verifies its
 * own output, then prints PASS/FAIL lines so the result can be checked
 * automatically.
 *
 * Requires -D MYCILA_JSON_SUPPORT and ArduinoJson (pio run -e json).
 */
#include <Arduino.h>
#include <ArduinoJson.h>
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

uint32_t helloRuns = 0;

void setup() {
  Serial.begin(115200);
  while (!Serial)
    continue;
  Serial.println("\n== Json example ==");

  sayHello.setInterval(1000);
  sayHello.onDone([](const Mycila::Task&, uint32_t) { helloRuns++; });
  loopTaskManager.addTask(sayHello);

  ping.setEnabled(true);
  loopTaskManager.addTask(ping);

  loopTaskManager.enableProfiling(6, 6, 1);

  // let a few runs happen so the profiling histogram is populated
  uint32_t start = millis();
  while (millis() - start < 3500)
    loopTaskManager.loop(10);

  // serialize the manager, print it, then parse it back
  JsonDocument doc;
  loopTaskManager.toJson(doc.to<JsonObject>());
  serializeJson(doc, Serial);
  Serial.println();

  // verify our own output
  check(doc["name"] == "loop()", "manager name serialized");
  check(doc["state"] == "RUNNING", "manager state is RUNNING in JSON");
  check(doc["tasks"].size() == 2, "two tasks serialized");

  JsonObject hello = doc["tasks"][0];
  check(hello["name"] == "sayHello", "task 0 is sayHello");
  check(hello["type"] == "FOREVER", "task 0 type is FOREVER");
  check(hello["state"] == "IDLE", "task 0 state is IDLE (json is const, read between runs)");
  check(hello["paused"] == false, "task 0 is not paused");

  JsonObject pj = doc["tasks"][1];
  check(pj["name"] == "ping", "task 1 is ping");
  check(pj["type"] == "ONCE", "task 1 type is ONCE");
  check(pj["paused"] == true, "task 1 is paused (ONCE, never resumed)");

  // profiling stats must appear once the task ran
  check(hello["stats"]["count"] >= 1, "sayHello stats were recorded");

  loopTaskManager.log();
  check(helloRuns >= 2, "sayHello ran during the warmup");

  Serial.printf("== RESULT: %d passed, %d failed ==\n", g_pass, g_fail);
  Serial.println("== Json example complete ==");
}

void loop() {
  vTaskDelay(portMAX_DELAY);
}