#pragma once
#include "ITelemetrySource.h"
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

private:
    bool connectBt();
    void scanForDevices();   // inquiry scan → fills _diag (names + MACs)
    // Polls one ELMduino PID call until it completes (non-blocking lib, bounded wait).
    // Templated because ELMduino getters return mixed types (e.g. kph() is int32_t,
    // rpm() is float) — accept any return type and store it as float.
    template <typename T>
    bool queryPid(T (ELM327::*fn)(), float& out, uint32_t timeoutMs = 250);

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
};
