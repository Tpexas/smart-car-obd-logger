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
- [ ] **Fuel accuracy fix** (found on first real drive): PID polling desync froze MAF,
      and fixed-AFR MAF math overestimates diesel fuel → switched to PID 015E (direct
      fuel rate) with load-corrected MAF fallback. Needs one more drive to verify.
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

### Phase 6 — Weather correlation (n8n) (~half day)
- [ ] n8n: at trip end, fetch **Open-Meteo** (free, no key) for the trip's
      time/location, write wind/temp/pressure into the `trips` row
- [ ] Grafana: scatter fuel/economy vs headwind, temp, etc.

> **You can show:** "does cold/wind hurt my MPG?" — a genuine insight.

### Phase 7 — GPS + route maps (~1 day + ~$5 module)
- [ ] Add a NEO-6M/M8N GPS module (UART), log lat/lon per record
- [ ] ThingsBoard/Grafana **map** of each route
- [ ] Per-location weather instead of a single city point

> **You can show:** a map of where you drove, per trip.

### Phase 8 — Polish & extras
- [ ] Trip auto-naming, a "trip detail" dashboard, anomaly flags (overheating, low
      voltage), email/Telegram alert via n8n on a bad reading
- [ ] Final portfolio writeup: architecture diagram, the story, screenshots, lessons

---

## Architecture notes & decisions (FAQ)

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
