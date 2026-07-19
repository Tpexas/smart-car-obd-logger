#include "ELM327Source.h"
#include "esp_bt.h"

bool ELM327Source::connectBt() {
    _connected = false;
    _status = "bt-connecting";
    Serial.printf("[ELM] Bluetooth master, connecting to \"%s\" ...\n", _btName);

    // We only use Bluetooth Classic (SPP). Release the BLE controller memory once
    // (~30 KB) so there's more heap left for the WiFi TLS handshake. Must happen
    // before the BT controller is initialised by _serialBT.begin().
    static bool bleReleased = false;
    if (!bleReleased) {
        esp_bt_controller_mem_release(ESP_BT_MODE_BLE);
        bleReleased = true;
        Serial.printf("[Heap] before BT init: %u bytes free\n", ESP.getFreeHeap());
    }

    // Master mode so the ESP32 initiates the connection to the adapter.
    if (!_serialBT.begin("SmartCarESP32", true)) {
        _status = "bt-init-failed";
        Serial.println("[ELM] BluetoothSerial begin() failed");
        return false;
    }
    Serial.printf("[Heap] after BT controller init: %u bytes free\n", ESP.getFreeHeap());

    // Cheap ELM327 clones use legacy pairing with a fixed PIN (usually 1234).
    _serialBT.setPin(_btPin);

    // Prefer connecting by MAC if one is configured — it skips the name-lookup
    // inquiry scan, which is more reliable. Otherwise fall back to the name.
    bool ok;
    uint8_t mac[6];
    if (strlen(_btMac) >= 17 &&
        sscanf(_btMac, "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx",
               &mac[0], &mac[1], &mac[2], &mac[3], &mac[4], &mac[5]) == 6) {
        Serial.printf("[ELM] connecting by MAC %s ...\n", _btMac);
        ok = _serialBT.connect(mac);
    } else {
        Serial.printf("[ELM] connecting by name \"%s\" ...\n", _btName);
        ok = _serialBT.connect(_btName);
    }

    if (!ok) {
        _status = "bt-connect-failed";
        Serial.println("[ELM] BT connect failed — scanning to see what's nearby ...");
        if (!_scanned) { scanForDevices(); _scanned = true; }   // once — publishes names+MACs
        return false;
    }

    Serial.println("[ELM] BT link up, initialising ELM327 ...");
    if (!_elm.begin(_serialBT, false /*debug*/, 2000 /*timeout ms*/)) {
        _status = "elm-init-failed";
        Serial.println("[ELM] ELM327 begin() failed (no response to AT commands)");
        return false;
    }

    _connected = true;
    _status = "obd-ready";
    _diag = "obd-ready";
    Serial.println("[ELM] OBD ready");
    return true;
}

// Inquiry scan for nearby Bluetooth Classic devices; records their names + MACs
// into _diag so main can publish it over MQTT (lets us see, without a laptop,
// whether the ELM327 is even visible and what its MAC is).
void ELM327Source::scanForDevices() {
    BTScanResults* res = _serialBT.discover(10000);   // ~10s inquiry
    if (!res || res->getCount() == 0) {
        _diag = "scan:none";   // nothing visible — adapter off, out of range, or busy with a phone
        Serial.println("[ELM] scan: no BT devices found");
        return;
    }
    String r = "scan:";
    int n = res->getCount();
    for (int i = 0; i < n && r.length() < 380; i++) {
        BTAdvertisedDevice* d = res->getDevice(i);
        r += " [";
        r += String(d->getName().c_str());
        r += "|";
        r += String(d->getAddress().toString().c_str());
        r += "]";
    }
    _diag = r;
    Serial.printf("[ELM] %s\n", r.c_str());
}

bool ELM327Source::begin() {
    _lastConnectAttempt = millis();
    return connectBt();
}

template <typename T>
bool ELM327Source::queryPid(T (ELM327::*fn)(), float& out, uint32_t timeoutMs) {
    // Keep calling the SAME getter until ELMduino reports a final state. Never
    // abandon mid-query: that leaves the reply in the BT buffer and the next PID
    // reads it as its own answer → frozen/garbage values. ELMduino's internal 2s
    // timeout resolves a dead query; timeoutMs is only a safety net above that.
    uint32_t start = millis();
    for (;;) {
        T v = (_elm.*fn)();
        if (_elm.nb_rx_state == ELM_SUCCESS)     { out = (float)v; return true; }
        if (_elm.nb_rx_state != ELM_GETTING_MSG) { return false; }   // PID unsupported / error
        if (millis() - start > timeoutMs)        { return false; }   // BT likely dead
        delay(2);
    }
}

bool ELM327Source::read(TelemetrySnapshot& out) {
    // Reconnect if the Bluetooth link dropped (bounded retry, non-blocking-ish).
    if (!_serialBT.connected()) {
        _connected = false;
        _status = "bt-dropped";
        if (millis() - _lastConnectAttempt > 5000) {
            _lastConnectAttempt = millis();
            connectBt();
        }
        return false;
    }

    out = _last;   // keep last-known values for any PID that doesn't answer this cycle

    // Every cycle: the signals we integrate per-trip (distance & fuel) plus RPM.
    queryPid(&ELM327::rpm, out.rpm);
    queryPid(&ELM327::kph, out.speedKph);

    // Fuel — prefer PID 015E (direct L/h, correct for diesel). Probe a few times
    // at startup; fall back to the MAF+load estimate if the car never answers.
    if (wants(SIG_FUEL_RATE) || wants(SIG_MAF)) {
        if (_useFuelRatePid || _fuelProbesLeft > 0) {
            float fr;
            if (queryPid(&ELM327::fuelRate, fr)) {
                _useFuelRatePid = true;
                _fuelProbesLeft = 0;
                out.fuelRateLph = fr;
            } else if (!_useFuelRatePid) {
                _fuelProbesLeft--;
            }
        }
        if (!_useFuelRatePid && _fuelProbesLeft <= 0) {
            // MAF fallback needs engine load fresh (the diesel AFR model scales with it).
            queryPid(&ELM327::engineLoad, out.engineLoadPct);
            float maf;
            if (queryPid(&ELM327::mafRate, maf)) {
                out.mafGs       = maf;
                out.fuelRateLph = fuelRateLphFromMaf(maf, _diesel, out.engineLoadPct);
            }
        }
    }

    // Fuel rail gauge pressure (PID 0123). Polled every cycle when active — it
    // spikes fast under demand, and actual-below-desired at high load is the
    // earliest Bosch CP4 pump-wear signal.
    if (wants(SIG_RAIL)) {
        float railKpa;
        // NB: "Guage" is ELMduino's own (misspelled) method name for PID 0123.
        if (queryPid(&ELM327::fuelRailGuagePressure, railKpa)) {
            out.railPressureBar = railKpa / 100.0f;   // kPa -> bar
        }
    }

    // Manifold absolute pressure (PID 010B) — boost. Polled every cycle when
    // active: boost transients under load are exactly what we're after. Idle
    // ≈ 100 kPa (ambient); a healthy 2.0 TDI full pull should hit ~230-250 kPa.
    // Low MAP + high load + black smoke = turbo/actuator/boost-leak problem.
    if (wants(SIG_MAP)) {
        float mapKpa;
        if (queryPid(&ELM327::manifoldPressure, mapKpa)) {
            out.mapKpa = mapKpa;
        }
    }

    // Secondary, slow-changing PIDs — rotate one active one per cycle to keep each
    // read() within the 1 Hz publish budget.
    switch (_cycle++ % 6) {
        case 0: if (wants(SIG_COOLANT))  queryPid(&ELM327::engineCoolantTemp, out.coolantTempC);   break;
        case 1: if (wants(SIG_INTAKE))   queryPid(&ELM327::intakeAirTemp,     out.intakeTempC);    break;
        case 2: if (wants(SIG_THROTTLE)) queryPid(&ELM327::throttle,          out.throttlePct);    break;
        case 3: if (wants(SIG_LOAD))     queryPid(&ELM327::engineLoad,        out.engineLoadPct);  break;
        case 4: if (wants(SIG_VOLTAGE))  queryPid(&ELM327::batteryVoltage,    out.batteryVoltage); break;
        case 5: if (wants(SIG_FUEL))     queryPid(&ELM327::fuelLevel,         out.fuelLevelPct);   break;
    }

    out.timestampMs = millis();
    _last = out;
    _status = _useFuelRatePid ? "obd-ready(fuel-pid)" : "obd-ready(maf-est)";
    return true;
}
