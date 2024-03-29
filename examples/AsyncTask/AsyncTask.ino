#include <Arduino.h>
#include <MycilaTaskManager.h>

Mycila::Task sayHello("sayHello", [](void* params) { Serial.println("Hello"); });

void setup() {
  Serial.begin(115200);
  while (!Serial)
    continue;

  sayHello.setType(Mycila::TaskType::FOREVER);
  sayHello.setInterval(1 * Mycila::TaskDuration::SECONDS);
  sayHello.setCallback([](const Mycila::Task& me, const uint32_t elapsed) {
    ESP_LOGD("app", "Task '%s' executed in %u ms", me.getName(), elapsed / Mycila::TaskDuration::MILLISECONDS);
  });

  sayHello.asyncStart();
}

void loop() {
  vTaskDelete(NULL);
}
