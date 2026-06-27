#pragma once
#include "ITelemetrySource.h"

// Generates realistic-looking OBD telemetry without any hardware, so the whole
// device -> cloud pipeline can be built and tested from a desk.
//
// It runs a simple drive-cycle state machine (idle -> accelerate -> cruise ->
// decelerate -> idle ...). Speed follows the phase, RPM is derived from speed via
// simulated gears, and coolant temperature warms up from ambient over time — so
// the dashboards look like a real trip, not random noise.
class SimulatorSource : public ITelemetrySource {
public:
    bool begin() override;
    bool read(TelemetrySnapshot& out) override;
    const char* name() const override { return "Simulator"; }

private:
    enum class Phase { Idle, Accelerate, Cruise, Decelerate };

    void advancePhase(uint32_t now);
    float rpmFromSpeed(float speedKph, float throttlePct) const;

    Phase    _phase        = Phase::Idle;
    uint32_t _phaseEndMs   = 0;
    uint32_t _lastUpdateMs = 0;
    uint32_t _startedMs    = 0;

    float _speedKph     = 0.0f;
    float _throttlePct  = 0.0f;
    float _coolantTempC = 20.0f;   // starts at ambient
    float _fuelLevelPct = 75.0f;
};
