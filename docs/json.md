# JSON Output

When compiled with `MYCILA_JSON_SUPPORT`, the manager and every task can serialize themselves into an ArduinoJson object. This is intended for telemetry: send the whole task tree over MQTT, HTTP or Serial in one call.

## Enabling

```ini
build_flags =
  -D MYCILA_JSON_SUPPORT

lib_deps =
  bblanchon/ArduinoJson
```

## Exporting

```cpp
#include <ArduinoJson.h>
#include <MycilaTaskManager.h>

void outputJson() {
  JsonDocument doc;
  tasks.toJson(doc.to<JsonObject>());
  serializeJson(doc, Serial);
  Serial.println();
}
```

## Output Shape

```json
{
  "name": "loop()",
  "stats": {
    "count": 15,
    "unit_divider": 1,
    "bins": [3, 12, 0, 0, 0, 0]
  },
  "tasks": [
    {
      "name": "sayHello",
      "type": "FOREVER",
      "paused": false,
      "enabled": true,
      "interval": 1000,
      "state": "IDLE",
      "stats": {
        "count": 15,
        "unit_divider": 1,
        "bins": [0, 15, 0, 0, 0, 0]
      }
    },
    {
      "name": "ping",
      "type": "ONCE",
      "paused": true,
      "enabled": true,
      "interval": 0,
      "state": "IDLE"
    }
  ]
}
```

### Task fields

| Field      | Type    | Description                                    |
| :--------- | :------ | :--------------------------------------------- |
| `name`     | string  | Task name                                      |
| `type`     | string  | `"ONCE"` or `"FOREVER"`                        |
| `paused`   | boolean | Current pause state                            |
| `enabled`  | boolean | Result of the enabled predicate                |
| `interval` | number  | Interval in milliseconds                       |
| `state`    | string  | `"IDLE"`, `"RUNNING"` or `"FINALIZING"`        |
| `stats`    | object  | Present only if profiling is enabled **and** data was recorded |

### Statistics fields

| Field          | Type   | Description                                     |
| :------------- | :----- | :---------------------------------------------- |
| `count`        | number | Number of recorded executions                   |
| `unit_divider` | number | Unit divider used when recording                |
| `bins`         | array  | Count per power-of-2 bin                        |

## Notes

- `toJson()` is `const`, so it is safe to call from a read-only telemetry task.
- The `stats` sub-object is emitted only when `_stats` exists **and** both `bins()` and `count()` are non-zero — an idle task produces a compact entry.
- The manager always emits a `tasks` array, even when it is empty.
- Neither `lastEndTime()` nor `remainingTime()` is exported: the JSON describes configuration and profiling, not the live scheduling position. Query them through the API if you need them.
- `state` is instantaneous and racy by nature: it reports what was true when `toJson()` read it, which is only meaningful while nobody else is driving the manager.
- Both `Task::toJson()` and `BinStatistics::toJson()` are compiled away unless `MYCILA_JSON_SUPPORT` is defined, so there is zero cost when the feature is off.
- Combine with [Profiling & Statistics](statistics.md) — without profiling there is no `stats` key.

## Minimal Telemetry Task

```cpp
tasks.newTask("telemetry", [](void* params) {
  JsonDocument doc;
  tasks.toJson(doc.to<JsonObject>());
  serializeJson(doc, Serial);
  Serial.println();
  tasks.log();
}).setInterval(5000);
```
