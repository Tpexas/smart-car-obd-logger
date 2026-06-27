-- Smart Car — trip logger schema (Postgres)
-- Runs in a dedicated `postgres` container (DB/user/owner: smartcar).
-- Apply with:  docker exec -i postgres psql -U smartcar -d smartcar < db/schema.sql

-- Raw telemetry: one row per reading, stamped with the device's trip_id + real time.
CREATE TABLE IF NOT EXISTS telemetry (
  id          BIGSERIAL   PRIMARY KEY,
  trip_id     TEXT        NOT NULL,
  ts          TIMESTAMPTZ NOT NULL,
  rpm         REAL,
  speed       REAL,
  coolant     REAL,
  intake      REAL,
  throttle    REAL,
  engine_load REAL,
  voltage     REAL,
  fuel        REAL
);
CREATE INDEX IF NOT EXISTS idx_telemetry_trip ON telemetry(trip_id);
CREATE INDEX IF NOT EXISTS idx_telemetry_ts   ON telemetry(ts);

-- Per-trip summary: one row per trip, upserted by Node-RED at trip end.
-- Weather columns are filled later by n8n (Phase 6).
CREATE TABLE IF NOT EXISTS trips (
  trip_id          TEXT PRIMARY KEY,
  started_at       TIMESTAMPTZ,
  ended_at         TIMESTAMPTZ,
  duration_s       INTEGER,
  distance_km      REAL,      -- integral of speed over time
  avg_speed        REAL,
  max_speed        REAL,
  avg_rpm          REAL,
  max_rpm          REAL,
  max_coolant      REAL,
  min_voltage      REAL,
  samples          INTEGER,
  weather_temp_c   REAL,      -- (Phase 6, n8n)
  weather_wind_kph REAL,
  weather_desc     TEXT,
  created_at       TIMESTAMPTZ DEFAULT now()
);
