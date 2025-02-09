#include <Arduino.h>
#include <MycilaTaskManager.h>

Mycila::TaskManager loopTaskManager("loop()");

Mycila::Task sayHello("sayHello", [](void* params) { Serial.println("Hello"); delay(random(1, 500)); });
Mycila::Task sayGoodbye("sayGoodbye", [](void* params) { Serial.println("Hello"); delay(random(1, 500)); });
Mycila::Task ping("ping", [](void* params) { Serial.println((const char*)params); delay(random(1, 500)); });
Mycila::Task delayed("delayed", [](void* params) { Serial.println("Delayed!"); delay(random(1, 500)); });
Mycila::Task output("output", [](void* params) { loopTaskManager.log(); });

char* params = "Pong";

void setup() {
  Serial.begin(115200);
  while (!Serial)
    continue;

  sayHello.setType(Mycila::Task::Type::FOREVER);
  sayHello.setInterval(1000);
  sayHello.onDone([](const Mycila::Task& me, uint32_t elapsed) {
    ESP_LOGD("app", "Task '%s' executed in %" PRIu32 " us", me.name(), elapsed);
  });
  sayHello.setEnabled(true);
  loopTaskManager.addTask(sayHello);

  sayGoodbye.setType(Mycila::Task::Type::FOREVER);
  sayGoodbye.setInterval(3000);
  sayGoodbye.onDone([](const Mycila::Task& me, uint32_t elapsed) {
    ESP_LOGD("app", "Task '%s' executed in %" PRIu32 " us", me.name(), elapsed);
    ping.setData(params);
    ping.resume();
  });
  sayGoodbye.setEnabled(true);
  loopTaskManager.addTask(sayGoodbye);

  ping.setType(Mycila::Task::Type::ONCE);
  ping.onDone([](const Mycila::Task& me, uint32_t elapsed) {
    ESP_LOGD("app", "Task '%s' executed in %" PRIu32 " us", me.name(), elapsed);
  });
  ping.setEnabled(true);
  loopTaskManager.addTask(ping);

  output.setType(Mycila::Task::Type::FOREVER);
  output.setInterval(5000);
  output.setEnabled(true);
  loopTaskManager.addTask(output);

  delayed.setType(Mycila::Task::Type::ONCE);
  delayed.setEnabled(true);
  loopTaskManager.addTask(delayed);

  loopTaskManager.enableProfiling(6);

  delayed.resume(10000);
}

void loop() {
  loopTaskManager.loop();
}
