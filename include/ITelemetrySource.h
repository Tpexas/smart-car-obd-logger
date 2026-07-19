#pragma once
#include "TelemetrySnapshot.h"

class LogConfig;   // runtime logging selection (fwd decl — only a pointer is used)

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

    // Is the source currently delivering data? (e.g. ELM327 Bluetooth link up.)
    // The simulator is always "connected".
    virtual bool connected() const { return true; }

    // Short status string published to MQTT so the device state is visible
    // remotely (no serial cable needed).
    virtual const char* statusText() const { return "ok"; }

    // Optional longer diagnostic (e.g. a Bluetooth scan result) published to MQTT
    // once after startup. Empty = nothing to report.
    virtual const char* diag() const { return ""; }

    // Share the runtime log config so a source can poll only the active PIDs.
    virtual void setConfig(LogConfig*) {}
};
