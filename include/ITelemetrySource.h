#pragma once
#include "TelemetrySnapshot.h"

// A source of telemetry. The rest of the firmware programs against this interface,
// so swapping the simulator for the real ELM327 is a one-line change in main.cpp:
// just construct ELM327Source instead of SimulatorSource.
class ITelemetrySource {
public:
    virtual ~ITelemetrySource() {}

    // Initialise the source (open the connection, etc.). Returns false on failure.
    virtual bool begin() = 0;

    // Fill `out` with the latest reading. Returns false if no fresh data is available.
    virtual bool read(TelemetrySnapshot& out) = 0;

    // Human-readable name, used in logs.
    virtual const char* name() const = 0;
};
