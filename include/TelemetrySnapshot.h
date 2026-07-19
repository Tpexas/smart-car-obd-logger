#pragma once
#include <Arduino.h>

// One reading of the car's state at a moment in time.
// This is the common data model the whole pipeline speaks: whatever produces it
// (simulator now, ELM327 later) and whatever consumes it (MQTT publisher) only
// depend on this struct — not on each other.
struct TelemetrySnapshot {
    uint32_t timestampMs   = 0;   // millis() when sampled
    float    rpm           = 0;   // engine RPM
    float    speedKph      = 0;   // vehicle speed, km/h
    float    coolantTempC  = 0;   // engine coolant temperature, °C
    float    intakeTempC   = 0;   // intake air temperature, °C
    float    throttlePct   = 0;   // throttle position, 0..100 %
    float    engineLoadPct = 0;   // calculated engine load, 0..100 %
    float    batteryVoltage = 0;  // control module voltage, V
    float    fuelLevelPct  = 0;   // fuel tank level, 0..100 %
    float    mafGs         = 0;   // mass air flow, g/s (PID 0110) — basis for fuel calc
    float    fuelRateLph   = 0;   // computed instantaneous fuel rate, L/h
    float    railPressureBar = 0; // fuel rail gauge pressure, bar (PID 0123)
};

// Instantaneous fuel rate from MAF:  fuel(L/h) = MAF(g/s) * 3600 / (AFR * density).
//
// Petrol engines run closed-loop at stoichiometric AFR (~14.7), so a fixed AFR works.
// Diesels DON'T: they run lean, with AFR swinging from ~80 at idle to ~14.5 at full
// load — a fixed 14.5 overestimates fuel several-fold at light load. For diesel we
// approximate AFR from engine load (first-order model; prefer PID 015E when the car
// supports it — that's a direct fuel-rate reading and needs no model at all).
inline float fuelRateLphFromMaf(float mafGs, bool diesel, float engineLoadPct) {
    float afr, density;
    if (diesel) {
        float load = engineLoadPct < 15.f ? 15.f : (engineLoadPct > 100.f ? 100.f : engineLoadPct);
        afr = 14.5f * 100.0f / load;          // light load → very lean
        if (afr > 80.f) afr = 80.f;
        density = 832.0f;                      // g/L
    } else {
        afr     = 14.7f;
        density = 745.0f;
    }
    return mafGs * 3600.0f / (afr * density);
}
