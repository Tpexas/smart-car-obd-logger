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
};
