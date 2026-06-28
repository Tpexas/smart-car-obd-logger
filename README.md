# Smart Car — OBD-II Telemetry

ESP32 firmware that reads a car's OBD-II data and streams it over MQTT to a
self-hosted dashboard (ThingsBoard). Built to run **without the car** first:
a simulator generates realistic telemetry so the whole device → cloud pipeline
can be developed and demoed from a desk.

```
ELM327 (Bluetooth)            ESP32                  Phone hotspot (4G)        VPS
   [later]  ───BT-SPP──►  ┌──────────────┐  ──WiFi──►   ───────────►   ┌──────────────┐
                          │ TelemetrySrc │                              │  MQTT broker │
   Simulator ──────────►  │   → MQTT     │                              │  → Node-RED  │
   [now]                  └──────────────┘                              │  → ThingsBd  │
                                                                        └──────────────┘
```

## Dashboard

Live ThingsBoard dashboard driven by the simulator — gauges for RPM, speed,
coolant, voltage and fuel, plus a time-series chart showing the simulated drive
cycle (idle → accelerate → cruise → decelerate).

![Smart Car dashboard](docs/dashboard.png)

## Trip analysis (Grafana)

Per-trip summaries land in a Postgres `trips` table (one row per trip, computed by
Node-RED at trip end). Grafana — auto-provisioned from
[grafana/provisioning/](grafana/provisioning/) — visualizes them: totals, a trips
table, and bar charts comparing distance and speed across trips.

![Grafana trips dashboard](docs/grafana.png)

## Why the abstraction

The firmware programs against [`ITelemetrySource`](include/ITelemetrySource.h).
Today the concrete source is [`SimulatorSource`](src/SimulatorSource.cpp).
When the car is available, add an `ELM327Source` that implements the same
interface and change **one line** in [`main.cpp`](src/main.cpp) — the WiFi, MQTT,
JSON, and dashboards are untouched.

## Hardware

- ESP32 WROOM-32 dev board (Bluetooth Classic — needed for cheap ELM327 dongles)
- *(later)* ELM327 Bluetooth OBD-II adapter
- *(in-car later)* 12V→5V buck converter off the OBD port

## Build & flash (PlatformIO)

1. Copy the config template and fill in your values:
   ```
   cp include/config.example.h include/config.h
   ```
   `config.h` is git-ignored, so your WiFi/MQTT secrets stay off the repo.

2. Build & upload (VS Code PlatformIO toolbar, or CLI):
   ```
   pio run -t upload
   pio device monitor
   ```

## Where to send the data

The firmware publishes flat JSON like:

```json
{"rpm":1840,"speed":42.3,"coolant":88.1,"intake":31.2,
 "throttle":24.0,"load":35.1,"voltage":14.21,"fuel":74.6}
```

Two ways to point it at your server (set in `config.h`):

| Target | `MQTT_USERNAME` | `MQTT_PASSWORD` | `MQTT_TELEMETRY_TOPIC` |
|---|---|---|---|
| **ThingsBoard direct** (fastest to a dashboard) | device access token | *(empty)* | `v1/devices/me/telemetry` |
| **Your own Mosquitto** (then bridge via Node-RED) | broker user | broker pass | `smartcar/telemetry` |

## Roadmap

- [x] Simulator source + MQTT publishing
- [ ] ThingsBoard device + dashboard (gauges + time-series)
- [ ] Node-RED transform/bridge (optional)
- [ ] `ELM327Source` — real OBD over Bluetooth Classic
- [ ] In-car power + deep sleep when ignition off

## Security note

Don't expose MQTT (1883) to the internet unauthenticated. Use a broker
username/password and TLS (8883), or keep the broker behind the VPS firewall and
tunnel to it. The simulator/dev setup uses plain MQTT for convenience only.
