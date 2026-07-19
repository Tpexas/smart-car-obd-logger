#include "SimulatorSource.h"
#include "config.h"

namespace {
// Small helpers
float clampf(float v, float lo, float hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}
// Random float in [a, b]
float frand(float a, float b) {
    return a + (b - a) * (float)random(0, 10001) / 10000.0f;
}
// Move `current` toward `target` by at most `maxStep`.
float approach(float current, float target, float maxStep) {
    if (current < target) return min(current + maxStep, target);
    if (current > target) return max(current - maxStep, target);
    return current;
}
} // namespace

bool SimulatorSource::begin() {
    randomSeed((uint32_t)esp_random());
    uint32_t now   = millis();
    _startedMs     = now;
    _lastUpdateMs  = now;
    _phase         = Phase::Idle;
    _phaseEndMs    = now + 4000;   // idle for a few seconds before "driving off"
    return true;
}

void SimulatorSource::advancePhase(uint32_t now) {
    if (now < _phaseEndMs) return;

    // Cycle through a believable drive pattern with randomised durations.
    switch (_phase) {
        case Phase::Idle:
            _phase      = Phase::Accelerate;
            _phaseEndMs = now + (uint32_t)frand(4000, 9000);
            break;
        case Phase::Accelerate:
            _phase      = Phase::Cruise;
            _phaseEndMs = now + (uint32_t)frand(8000, 20000);
            break;
        case Phase::Cruise:
            // Sometimes accelerate again, sometimes slow down.
            if (random(0, 2) == 0) {
                _phase      = Phase::Accelerate;
                _phaseEndMs = now + (uint32_t)frand(3000, 7000);
            } else {
                _phase      = Phase::Decelerate;
                _phaseEndMs = now + (uint32_t)frand(4000, 9000);
            }
            break;
        case Phase::Decelerate:
            _phase      = Phase::Idle;
            _phaseEndMs = now + (uint32_t)frand(3000, 8000);
            break;
    }
}

float SimulatorSource::rpmFromSpeed(float speedKph, float throttlePct) const {
    if (speedKph < 1.5f) {
        return frand(780, 850);   // idling
    }
    // Simulate gears: each gear covers a speed band; within a band RPM rises,
    // then drops on the upshift — giving the classic sawtooth RPM trace.
    static const float gearTop[] = {20.f, 40.f, 62.f, 90.f, 130.f, 180.f};
    const int gears = sizeof(gearTop) / sizeof(gearTop[0]);

    float low = 0.f;
    for (int g = 0; g < gears; ++g) {
        float high = gearTop[g];
        if (speedKph <= high || g == gears - 1) {
            float frac = (speedKph - low) / max(1.0f, (high - low));
            frac = clampf(frac, 0.f, 1.f);
            float rpm = 1200.f + frac * 1800.f;          // 1200 -> 3000 across the gear
            rpm += throttlePct * 12.f;                   // throttle pushes revs up
            return clampf(rpm + frand(-40, 40), 700.f, 6500.f);
        }
        low = high;
    }
    return 1500.f;
}

bool SimulatorSource::read(TelemetrySnapshot& out) {
    uint32_t now = millis();
    float dt = (now - _lastUpdateMs) / 1000.0f;   // seconds since last sample
    _lastUpdateMs = now;
    if (dt <= 0) dt = 0.001f;

    advancePhase(now);

    // Target throttle/speed depend on the current phase.
    float targetThrottle = 0.f;
    float speedAccel      = 0.f;   // km/h per second
    switch (_phase) {
        case Phase::Idle:       targetThrottle = 0.f;  speedAccel = -12.f; break;
        case Phase::Accelerate: targetThrottle = frand(45, 80); speedAccel = 8.f;  break;
        case Phase::Cruise:     targetThrottle = frand(15, 28); speedAccel = 0.f;  break;
        case Phase::Decelerate: targetThrottle = 0.f;  speedAccel = -9.f; break;
    }

    _throttlePct = approach(_throttlePct, targetThrottle, 60.f * dt);
    _speedKph    = clampf(_speedKph + speedAccel * dt, 0.f, 180.f);

    // Coolant warms from ambient toward ~90 °C with a slow time constant.
    float coolantTarget = 90.f;
    _coolantTempC += (coolantTarget - _coolantTempC) * clampf(0.012f * dt, 0.f, 1.f);
    _coolantTempC = clampf(_coolantTempC + frand(-0.05f, 0.05f), 15.f, 105.f);

    // Fuel slowly drops; loops back so the chart stays interesting on long runs.
    _fuelLevelPct -= 0.0008f * dt * (1.f + _throttlePct / 100.f);
    if (_fuelLevelPct < 5.f) _fuelLevelPct = 95.f;

    out.timestampMs    = now;
    out.speedKph       = _speedKph;
    out.throttlePct    = _throttlePct;
    out.rpm            = rpmFromSpeed(_speedKph, _throttlePct);
    out.engineLoadPct  = clampf(15.f + _throttlePct * 0.8f + frand(-3, 3), 0.f, 100.f);
    out.coolantTempC   = _coolantTempC;
    out.intakeTempC    = clampf(22.f + _coolantTempC * 0.1f + frand(-1, 1), 10.f, 60.f);
    out.batteryVoltage = frand(14.0f, 14.4f);          // engine running = alternator charging
    out.fuelLevelPct   = _fuelLevelPct;

    // MAF roughly scales with revs and load; idle ~3 g/s, hard pull ~40+ g/s.
    out.mafGs       = clampf(3.0f + (out.rpm / 1000.0f) * (out.engineLoadPct / 100.0f) * 14.0f
                             + frand(-0.3f, 0.3f), 1.5f, 60.0f);
    out.fuelRateLph = fuelRateLphFromMaf(out.mafGs, FUEL_IS_DIESEL, out.engineLoadPct);

    // Common-rail diesel rail pressure: ~300 bar at idle, climbing with revs and
    // load toward ~1800 bar at full demand.
    out.railPressureBar = clampf(300.0f + (out.engineLoadPct / 100.0f) * (out.rpm / 4500.0f) * 1450.0f
                                 + frand(-15.0f, 15.0f), 260.0f, 1850.0f);

    // Turbo boost: ambient (~100 kPa) at idle, rising with load toward ~2.4 bar abs.
    out.mapKpa = clampf(100.0f + (out.engineLoadPct / 100.0f) * 140.0f + frand(-3.0f, 3.0f),
                        95.0f, 250.0f);
    return true;
}
