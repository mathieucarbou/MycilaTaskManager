#include <Arduino.h>
#include <MycilaTaskManager.h>

Mycila::TaskManager taskManager1("tm-1");
Mycila::TaskManager taskManager2("tm-2");

Mycila::Task slowdown1("slowdown1", [](void* params) {
  long d = random(1000, 6000);
  Serial.printf("[slowdown1] Delaying for %ld ms\n", d);
  delay(d);
});

Mycila::Task slowdown2("slowdown2", [](void* params) {
  long d = random(1000, 6000);
  Serial.printf("[slowdown2] Delaying for %ld ms\n", d);
  delay(d);
});

void setup() {
  Serial.begin(115200);
  while (!Serial)
    continue;

  slowdown1.setType(Mycila::Task::Type::FOREVER);
  taskManager1.addTask(slowdown1);

  slowdown2.setType(Mycila::Task::Type::FOREVER);
  taskManager2.addTask(slowdown2);

  Mycila::TaskManager::configureWDT(5, false);

  taskManager1.asyncStart(4096, -1, -1, 10, true);
}

void loop() {
  taskManager2.loop();
}
