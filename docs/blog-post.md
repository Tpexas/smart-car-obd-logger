# I turned my diesel into a database: building an OBD-II trip logger with an ESP32


Every trip my car makes now ends as a row in a database: distance, duration, fuel
burned, average speed, engine warm-up time, battery voltage — and the weather it
was driven in. The hardware cost about €15: a €5 ELM327 Bluetooth OBD-II adapter
and an ESP32 dev board. Everything else is software I run myself.

This is the story of building it — including the three trips to the car where
nothing worked, the reasons why, and what a €5 Bluetooth clone teaches you about
embedded networking that no tutorial does.

## The idea

I didn't want live car monitoring — I wanted **history**. Which route to work is
actually faster? What does winter do to my fuel consumption? Is the thermostat
slowly dying? Those are questions about *trends across trips*, not about watching
RPM in real time.

So the shape of the project: plug the OBD adapter in when I drive, stream data
through my phone's hotspot to my own server, and let every trip accumulate into
a queryable history. No SD cards to shuffle, no leaving hardware in the car
draining the battery.

```
       CAR                    ESP32                     phone hotspot (4G)
 ┌────────────┐   Bluetooth   ┌────────────────┐   WiFi ┌──────────────────────────┐
 │   ELM327   │────Classic───►│ read OBD PIDs  │──TLS──►│ VPS (Docker)             │
 │ (OBD port) │      SPP      │ → JSON → MQTT  │        │  Mosquitto MQTT broker   │
 └────────────┘               └────────────────┘        │   ├► Node-RED            │
                                                        │   │   ├► ThingsBoard     │
                                                        │   │   │  (live gauges)   │
                                                        │   │   └► PostgreSQL      │
                                                        │   │      telemetry+trips │
                                                        │   └ trip end → n8n       │
                                                        │        └► Open-Meteo     │
                                                        │  Grafana ◄─ PostgreSQL   │
                                                        └──────────────────────────┘
```

The 1 Hz telemetry lands in a `telemetry` table. When the stream goes quiet for
90 seconds, Node-RED declares the trip over and computes a summary row: distance
(integrating speed over time), fuel used (integrating fuel rate), averages,
maxima, warm-up time — then asks n8n to fetch the weather from Open-Meteo and
stamps that on the trip too.

## Rule one: don't let the car block you

I made one decision at the very start that saved the project: **the firmware
talks to an interface, not to the car.**

```cpp
class ITelemetrySource {
    virtual bool begin() = 0;
    virtual bool read(TelemetrySnapshot& out) = 0;
};
```

The first implementation wasn't the ELM327 — it was a **simulator**: a little
drive-cycle state machine (idle → accelerate → cruise → decelerate) with RPM
derived from speed through simulated gear ratios and coolant that warms up
realistically. Fake car, real everything else.

That meant the entire pipeline — MQTT over TLS, the trips table, both dashboards,
weather enrichment — was built, tested and demoed from my desk before I ever sat
in the car. When the real `ELM327Source` finally worked, it replaced the
simulator by flipping one config flag, and every downstream system just... kept
working. If you build IoT projects and your hardware access is intermittent,
this is the highest-leverage trick I know.

## The Bluetooth saga, or: three trips to a car that wouldn't talk

The ELM327 clone speaks Bluetooth **Classic** (SPP) — which is why the board had
to be an original ESP32 WROOM-32; the newer S3/C3 chips are BLE-only and
physically cannot talk to these adapters. That part I knew going in. What
followed, I did not.

**Trip one: silence.** The ESP32 connected to the hotspot, and… nothing arrived
at the server. No laptop in the car, no serial output, no idea why. At home the
log showed the bug: my code did the (blocking) Bluetooth connect *before*
connecting MQTT — so when Bluetooth hung, the device never even reached the
broker to say what was wrong.

That failure produced the best architectural decision of the project: **the
device now reports its own state over MQTT** — `online`, `bt-connect-failed`,
`obd-ready` — as retained messages, plus a Bluetooth scan result topic listing
every device it can see. Retained means the broker keeps the last message, so
even hours later I could reconstruct exactly how far the device got. Every
subsequent in-car failure was diagnosed from my desk, over SSH, without ever
carrying a laptop to the car.

**Trip two: `bt-connect-failed`.** The status topic worked; Bluetooth didn't.
Was the phone still paired to the adapter and hogging it? (Bluetooth Classic
allows exactly one master — a phone that auto-reconnects to the adapter locks
the ESP32 out.) Phone Bluetooth off. Still nothing.

**The fix that finally worked:** connect by **MAC address** instead of device
name, with the pairing PIN (`1234`, printed on the AliExpress listing of all
places). Name-based connect needs an inquiry scan and a name lookup that these
clones seem to fail at; a direct MAC connect skips all of it. Next attempt:
`obd-ready`, and real engine data flowing into Postgres — RPM, coolant
temperature, alternator voltage, all live from the driveway.

## The RAM wall

The ESP32 WROOM has one radio and about 300 KB of usable RAM. Bluetooth Classic
takes a large bite (~60–90 KB), WiFi takes another, and a TLS handshake wants a
~40 KB *contiguous* allocation on top. All three at once do not really fit.

Measured on the device: ~124 KB free heap with MQTT/TLS up; ~39 KB right after
the Bluetooth stack initialised. If the MQTT connection ever dropped after
Bluetooth was running, the TLS *re*-handshake failed with
`SSL - Memory allocation failed`. Permanently.

The mitigations, in order of importance:

1. **Order matters.** Connect MQTT/TLS *first*, while the heap is whole. An
   established TLS session survives; it's only new handshakes that need the big
   allocation.
2. **Release what you don't use.** The BLE half of the Bluetooth controller can
   be freed at boot for ~30 KB back.
3. **Accept imperfection and self-heal.** If MQTT stays down too long after
   Bluetooth is up, reboot — a fresh boot has a whole heap again. The trip ID
   lives in RTC memory, which survives software resets, so a recovery reboot
   continues the *same* trip instead of splitting it in two.

First real drive: 28 minutes, 26 km, 1,433 samples, **zero gaps**, no self-heal
needed. Sometimes the paranoid engineering just quietly works.

## Diesel fuel math will humble you

The first real drive reported **12.4 L/100km** — roughly double what this car
actually burns. Two separate bugs, both instructive:

**The frozen sensor.** The MAF (mass air flow) readings showed only *five
distinct values* in 1,433 samples. Root cause: the OBD library (ELMduino) is
non-blocking — you're supposed to keep polling the *same* PID until it
completes. My code gave each query 250 ms and then moved on to the next PID,
which desynchronised the response stream: replies got matched to the wrong
queries and values froze. The fix: never abandon a query mid-flight, and rotate
the slow-changing PIDs (one per second) so the per-cycle budget holds.

**The petrol assumption.** The standard "fuel from MAF" formula divides airflow
by a fixed air-fuel ratio — 14.7:1, the stoichiometric ratio petrol engines hold
under closed-loop control. **Diesels don't work that way.** They run lean, with
AFR swinging from ~80 at idle to ~15 under full load. Feeding diesel MAF through
petrol math overestimates fuel severalfold at light load. The fix: read PID
015E (direct engine fuel rate) where the car supports it, and fall back to a
load-corrected AFR model only when it doesn't.

With both bugs fixed, I drove two ordinary commutes. The result: **4.3 and
4.4 L/100km** across two 25 km trips — down from the buggy 12.4, and consistent
with each other to within a tenth. The MAF sensor, previously frozen at five
distinct readings, now varies across ~880 distinct values per trip, tracking
every acceleration. Idle fuel rate settled at **0.5–0.65 L/h**, exactly where a
warm diesel idles. My car doesn't expose a direct fuel-rate PID, so these come
from the load-corrected MAF model — the ultimate cross-check, summing computed
litres against what the pump actually dispenses over a few tanks, is on the list.

## What it answers now

With the collection layer done, every drive feeds the analytics for free:

- **Routes without GPS:** different routes have different distances, and
  integrated-speed distance is accurate — so trips cluster into routes by
  distance signature alone. My commute shows up as `~26 km`; a future
  alternative route would land in its own bucket. GPS gets added later purely
  for the pretty maps.
- **Car health as trends:** time-to-80°C per trip (a thermostat that's dying
  shows up as a slowly lengthening warm-up), resting battery voltage on
  ignition-on starts, idle fuel rate drift. None of these mean anything from
  five trips — all of them mean something after a winter of commutes. The point
  of building collection early is that the clock is already running.
- **Weather × everything:** every trip carries temperature, wind and conditions,
  so "what does -10°C do to my consumption" becomes a SQL query.

*(Dashboard screenshots coming soon — the full code, schema, flows and Grafana
dashboards are on GitHub, linked below.)*

## Lessons that transfer

1. **Simulate the hardware you don't have yet.** An interface + a fake source
   kept the project unblocked for weeks of no car access.
2. **Make the device debuggable from where you are, not where it is.** Retained
   MQTT status topics turned every mystery failure into a five-minute diagnosis.
3. **On small chips, radios are a budget line.** BT Classic + WiFi + TLS is an
   allocation problem before it's a code problem. Order your connections.
4. **Cheap clones lie about names.** Connect by MAC, know the PIN.
5. **Non-blocking libraries mean it:** abandon a query mid-flight and you'll
   debug "impossible" frozen sensor values a week later.
6. **Physics beats formulas copied from forums.** The MAF fuel equation everyone
   posts is petrol-only. Know which engine you have.
7. **Ship something visible at every step.** Every phase ended with a screenshot
   in the README and a pushed commit. A project that's presentable at every
   stage is a project you can't quietly abandon.

## What's next

A GPS module for route maps, trouble-code (DTC) alerts pushed to my phone the
moment the engine logs one, and — mostly — just driving: the interesting graphs
need a few months of data, and the system now collects it every time I turn the
key.

Code, schema, flows and dashboards are all here:
**[github.com/Tpexas/smart-car-obd-logger](https://github.com/Tpexas/smart-car-obd-logger)**
