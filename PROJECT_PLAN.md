# Smart Car — Project Plan (trip logger)

**Goal:** a car *trip logger*. Plug the OBD reader in per trip, stream over 4G, and
build a **history** you can explore — compare trips, fuel consumption, and how
weather affects them. Not live monitoring; intermittent logging + analysis.

---

## How to use this plan (read this — it's the point)

You lose interest in projects. This plan is structured to fight that:

1. **Every phase ends with something to SHOW** (a screenshot / a real trip / a
   chart). If a phase doesn't produce a visible win, it's scoped wrong — cut it.
2. **There is a 🏁 SHIP IT line at Phase 5.** When you reach it, the project is
   *complete and portfolio-ready.* Everything after is optional bonus. You have
   explicit permission to stop there.
3. **Commit + push after every phase.** Update the README with one screenshot each
   time. This means a presentable project exists *at all times* — even if you quit
   mid-way, what's done is already shippable.
4. **Time-box each phase to one sitting / one weekend.** If it spills over, it's
   too big — shrink it.
5. **The simulator stays working forever.** You can always make progress without
   the car. The real ELM327 is *additive*, never a blocker.

Status: `[ ]` todo · `[~]` in progress · `[x]` done

---

## Phase 0 — Pipeline (DONE ✅)
- [x] ESP32 firmware, simulator data source
- [x] MQTT over TLS → Mosquitto broker (`mqtt.<your-domain>:8883`)
- [x] Node-RED subscribed
- [x] ThingsBoard device receiving all 8 telemetry keys
- [x] Server secured (default ThingsBoard passwords changed)

> **You can show:** live data landing in ThingsBoard. The hard plumbing is done.

---

## Phase 0.5 — Put it on GitHub (DONE ✅)
- [x] `git init`, commit the firmware project
- [x] Create a **public** GitHub repo, push → https://github.com/Tpexas/smart-car-obd-logger
- [x] Confirm `config.h` is git-ignored (secrets stay out) — it is

> **You can show:** a public repo link. This is a commitment device — it's "real"
> now. **Do every future phase as: build → commit → push → add a screenshot to the README.**

---

## Phase 1 — Dashboard (DONE ✅)
Built the ThingsBoard dashboard on the **simulator** data (no car needed).
- [x] Add widgets: speed gauge, RPM gauge, coolant, voltage, fuel level, throttle, load
- [x] Add a time-series chart (rpm + speed) — shows the realistic drive cycle
- [x] Tune ranges/colors (red zone on coolant, etc.)
- [x] Screenshot → README (`docs/dashboard.png`)

> **You can show:** a full gauge dashboard. First properly screenshot-worthy artifact.

---

## Phase 2 — Trips become first-class (DONE ✅)
This is what turns "telemetry" into a "trip log."
- [x] **Firmware:** generate a `trip_id` at boot + a real epoch timestamp in every message
- [x] **Postgres:** dedicated `postgres` container; `telemetry` (raw) + `trips` tables (`db/schema.sql`)
- [x] **Node-RED:** `nodered/trip-logger-flow.json` — inserts raw rows; on 90s idle
      (trip end) upserts a summary: duration, distance (∫ speed dt), avg/max speed, avg/max RPM
- [x] Verified: a trip computed correctly (48s, 0.29 km, avg 22.4 km/h, 49 samples)

> **You can show:** a `trips` table that grows by one row each time you "drive."
> This is the backbone of the whole logger.

---

## Phase 3 — Offline resilience (~half day) *(do before real road trips)*
So a dropped 4G signal doesn't punch holes in a trip.
- [ ] **Firmware:** buffer to **LittleFS** (internal flash — no SD needed) when MQTT
      is down, with real epoch timestamps
- [ ] Backfill the buffer when the link returns
- [ ] Test by killing WiFi mid-run and watching it catch up
- [ ] *(Optional)* add a microSD module only if you want an offline-first raw log

> **You can show:** pull the WiFi, plug it back, no data gap. Nice reliability story.

---

## Phase 4 — REAL CAR 🚗 (mostly DONE ✅ — connection verified)
The big payoff. Plugging in the car now produces real OBD data.
- [x] **`ELM327Source`** (Bluetooth Classic to the ELM327) — one-line switch via `USE_ELM327`
- [x] Connect by **MAC + PIN 1234** (name lookup was unreliable); confirmed car exposes MAF/fuel
- [x] Fuel calc: MAF → instantaneous fuel rate (diesel constants) in the payload
- [x] BT + WiFi + TLS coexistence handled (MQTT-first startup, BLE mem release,
      self-heal reboot with trip_id preserved in RTC) — stable in driveway test
- [x] Verified end-to-end: `obd-ready` + real telemetry in Postgres (engine off: rpm/speed 0, 12 V)
- [x] **Drive one real trip with the engine running** — 26 km / 28 min logged with
      ZERO data gaps (1433 samples); trip summary + fuel columns computed
- [x] Store fuel per trip: `maf`/`fuel_rate` columns + `fuel_used_l`/`l_per_100km` aggregation
- [x] **Fuel accuracy fix VERIFIED** (two commutes, 2026-07-08): freeze bug gone
      (880/835 distinct MAF values), realistic economy **4.3 & 4.4 L/100km** (was
      12.4), idle **0.48-0.65 L/h**. Car has no PID 015E → load-corrected MAF
      estimate (`maf-est`) is the active path. Optional further validation:
      tank-refill cross-check over several tanks.
- [ ] Power: 12V→5V buck off the OBD port (or USB power bank for now)

> **You can show:** your *actual car's* data in the dashboard. The "it really works"
> moment — achieved. Just needs a real drive to fill in the moving-vehicle values.

---

## 🏁 SHIP IT LINE — after Phase 5 the project is DONE & portfolio-ready

## Phase 5 — Analysis dashboard: Grafana (DONE ✅ on simulated trips)
- [x] Add **Grafana** container, connect to the Postgres `trips` table
- [x] Auto-provisioned datasource + dashboard (`grafana/provisioning/`)
- [x] Comparison panels: totals, trips table, distance/speed bar charts per trip
- [x] Screenshot → README (`docs/grafana-trips.png`)
- [ ] *(later)* public URL behind nginx (grafana.mazured.com) — currently via SSH tunnel

> **You can show:** "here are my last N trips compared." The analysis surface is
> built. Once **real car** data (Phase 4) flows in, this fills with real trips and
> the project is a complete, finished result.

---

## Bonus phases (do only if still having fun — zero guilt if you skip)

### Phase 6 — Weather correlation (n8n) (DONE ✅)
- [x] n8n webhook workflow (`n8n/trip-weather-workflow.json`): fetches **Open-Meteo**
      current weather (Kretinga–Klaipėda midpoint 55.80, 21.19 — towns are ~25 km
      apart, meteorologically identical; GPS would add nothing for weather)
- [x] Node-RED: at trip end → n8n webhook → temp/wind/desc written into the `trips` row
      (verified end-to-end against the first real trip)
- [ ] Grafana: scatter fuel/economy vs wind, temp — build once a few real trips
      with correct fuel numbers accumulate

> **You can show:** "does cold/wind hurt my MPG?" — a genuine insight.

### Phase 7 — GPS + route maps (WAITING on hardware 📦)
Decision (2026-07-04): use a dedicated GPS module, not phone GPS (OwnTracks was
considered and declined — battery/background-reliability trade-offs).
- [ ] **Order:** u-blox NEO-6M ("GY-GPS6MV2") or NEO-M8N board with ceramic
      antenna, UART interface, ~€5–8
- [ ] Wire to ESP32 UART2 (VCC/GND/TX→RX2, RX→TX2), TinyGPS++ in firmware,
      lat/lon in the payload + `gps_points` table
- [ ] Grafana **geomap** route per trip; ThingsBoard map widget
- [ ] Per-location weather instead of the fixed midpoint (marginal gain here)

> **You can show:** a map of where you drove, per trip.

### Phase 8 — Route & commute analysis (collection layer DONE ✅)
Distinct routes have distinct distance signatures (route A ≈ 12.3 km, route B ≈ 14.1 km),
so trips can be clustered by distance alone — GPS only needed for maps.
- [x] `route` label auto-assigned at trip end (2 km distance bucket, e.g. `~26 km`);
      manual renames survive re-summarize (COALESCE in the upsert)
- [x] Grafana: route-averages table (trips, avg duration/distance/L-100km)
- [ ] Duration by weekday / departure time — build after a few weeks of commutes
- [ ] Season/weather cross-analysis (weather columns already filling per trip)

> **You can show:** "route B is 4 min faster on average, but only in summer."

### Phase 9 — Car health monitoring (collection layer DONE ✅, trends need months)
All derivable from data already collected (plus DTC codes, one new firmware read):
- [x] **Warm-up time** (`warmup_s`): trip start → coolant 80 °C, per trip + Grafana
      trend — creeping upward = thermostat/cooling degradation (classic early symptom)
- [x] **Battery health** (`start_voltage`): first voltage sample per trip + Grafana
      trend; engine-off starts show true resting V (12.0 V baseline captured)
- [x] **Idle fuel rate** (`idle_fuel_lph`): avg fuel at idle per trip — drift
      suggests air path / injector issues (values valid after the fuel-accuracy fix)
- [ ] **Fuel-economy trend on the same route** — query once real trips accumulate
- [ ] **DTC trouble codes** (firmware; do AFTER the fuel fix is verified) —
      publish to MQTT, alert via n8n when the car logs a new code
- Note: trend verdicts need **months** of trips — the point of shipping collection now

> **You can show:** "my dashboard warned the thermostat was dying before the car did."

### Phase 10 — Polish & extras
- [ ] Trip auto-naming, a "trip detail" dashboard, anomaly flags (overheating, low
      voltage), email/Telegram alert via n8n on a bad reading
- [ ] Final portfolio writeup: architecture diagram, the story, screenshots, lessons

---

## Architecture notes & decisions (FAQ)

- **2026-07-19 — server migration.** Oracle reclaimed the original Always Free VPS
  (idle policy); trip history to that date was lost (Plan C: accepted, no recovery).
  Stack rebuilt **from this repo** in one session on the portfolio server
  (92.5.59.106): Mosquitto (same `mqtt.mazured.com:8883` + same device creds, so
  firmware needed zero changes), Postgres, Node-RED, Grafana, n8n.
  **ThingsBoard was dropped** — Grafana is the sole dashboard now. Lesson
  reinforced: everything-as-code in git made the rebuild ~1 hour; prevention:
  consider PAYG upgrade (free tier stays free, reclamation stops).
- **Runtime-selectable logging (2026-07):** the device subscribes to retained
  `smartcar/config`; presets `standard` / `fuel-health` / `full` (or explicit
  signal lists) switch what's polled+published live, **no re-flash**. Preset
  buttons in Node-RED. Added **fuel rail pressure** (PID 0123) for Bosch CP4
  pump-health monitoring (see Obsidian note "TA ir kuro sistema 2026-07"):
  watch rail pressure sagging under high load + injector corrections via VCDS.

- **Where's Postgres?** It's *bundled inside* the `thingsboard` container (the
  `tb-postgres` image = ThingsBoard + PostgreSQL together). That's why there's no
  separate `postgres` box in `docker ps`. For *our* `trips` data we add a **dedicated
  `postgres` container** (Phase 2) rather than polluting ThingsBoard's DB.
- **Grafana / trips table don't exist yet** — they're *created* in Phases 5 / 2,
  not missing.
- **Phone's role = dumb 4G gateway only.** It provides internet; it does *not* store
  or process data. Buffering happens on the ESP32 (LittleFS). Using the phone as a
  storage/buffer node would require custom always-on phone software (fragile,
  OS-killed in background) — not worth it vs the ESP32's self-contained flash. Keeping
  the device autonomous (works on any internet link) is also the better portfolio story.
- **GPS options (bonus Phase 7):**
  1. *ESP32 GPS module (NEO-6M, ~$5)* — self-contained, cleanest embedded story.
  2. *Phone GPS, no-dev* — record a GPX with any phone GPS-logger app, merge with the
     trip by timestamp afterward in Node-RED/a script. Zero hardware, good for
     occasional trips.
  3. *Phone GPS, integrated* — a small Android app reads GPS + publishes to MQTT;
     server merges with OBD by timestamp. No hardware but needs app dev.
  The phone can't hand its GPS to the ESP32 just by being the hotspot — location isn't
  exposed over the WiFi link; any phone-GPS path needs software on the phone.

## Definition of "good enough" (your finish line)
Real car data → logged trips → a dashboard that compares them. That's
**Phases 1–5.** If you do nothing past Phase 5, you have finished a real,
end-to-end IoT + cloud + data project worth showing. Bonus phases are flavor.

## The one rule that gets projects finished
After **every** phase: `commit → push → drop a screenshot in the README`.
A project that's presentable at every step is a project you can't really abandon —
you can only pause it with something already worth showing.
