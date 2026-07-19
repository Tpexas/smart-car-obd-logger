#pragma once
#include "ITelemetrySource.h"
#include "LogConfig.h"
#include "BluetoothSerial.h"
#include <ELMduino.h>

// Reads live OBD-II data from an ELM327 adapter over Bluetooth Classic (SPP).
// Implements the same ITelemetrySource interface as the simulator, so switching
// to real car data is a one-line change in main.cpp (USE_ELM327).
//
// Connects to the adapter by Bluetooth NAME (e.g. "OBDII") — no MAC needed.
// Publishes a short status string (connecting / ready / dropped) so the device
// state is visible over MQTT without a serial cable.
class ELM327Source : public ITelemetrySource {
public:
    ELM327Source(const char* btName, const char* btMac, const char* btPin, bool diesel)
        : _btName(btName), _btMac(btMac), _btPin(btPin), _diesel(diesel) {}

    bool begin() override;
    bool read(TelemetrySnapshot& out) override;
    const char* name() const override { return "ELM327"; }
    bool connected() const override { return _connected; }
    const char* statusText() const override { return _status; }
    const char* diag() const override { return _diag.c_str(); }
    void setConfig(LogConfig* cfg) override { _cfg = cfg; }

private:
    // Poll a PID only if its signal is active in the current runtime config.
    bool wants(uint32_t sig) const { return !_cfg || _cfg->isActive(sig); }

    bool connectBt();
    void scanForDevices();   // inquiry scan → fills _diag (names + MACs)
    // Polls one ELMduino PID call until it reaches a FINAL state (success or error).
    // ELMduino is non-blocking: abandoning a query mid-flight desyncs the response
    // stream and freezes values (root cause of the stuck MAF on the first real
    // drive) — so we never abandon; ELMduino's own 2s timeout bounds the wait.
    // Templated because ELMduino getters return mixed types (kph() int32_t, rpm() float).
    template <typename T>
    bool queryPid(T (ELM327::*fn)(), float& out, uint32_t timeoutMs = 2500);

    BluetoothSerial   _serialBT;
    ELM327            _elm;
    const char*       _btName;
    const char*       _btMac;
    const char*       _btPin;
    bool              _diesel;
    bool              _connected = false;
    bool              _scanned = false;
    const char*       _status = "boot";
    String            _diag;
    TelemetrySnapshot _last;
    uint32_t          _lastConnectAttempt = 0;

    // Fuel source: prefer direct engine fuel rate (PID 015E — correct for diesels,
    // no model needed). Probe it a few times at startup; if the car never answers,
    // fall back to the MAF estimate permanently.
    bool    _useFuelRatePid   = false;
    int8_t  _fuelProbesLeft   = 5;
    uint8_t _cycle            = 0;   // rotates the secondary PIDs, one per read()

    LogConfig* _cfg = nullptr;       // which signals to actually poll
};
