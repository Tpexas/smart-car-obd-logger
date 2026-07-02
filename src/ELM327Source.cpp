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
    uint32_t start = millis();
    while (millis() - start < timeoutMs) {
        T v = (_elm.*fn)();
        if (_elm.nb_rx_state == ELM_SUCCESS)     { out = (float)v; return true; }
        if (_elm.nb_rx_state != ELM_GETTING_MSG) { return false; }   // PID not supported / error
        delay(2);
    }
    return false;   // timed out this cycle (keeps previous value)
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

    queryPid(&ELM327::rpm,               out.rpm);
    queryPid(&ELM327::kph,               out.speedKph);
    queryPid(&ELM327::engineCoolantTemp, out.coolantTempC);
    queryPid(&ELM327::intakeAirTemp,     out.intakeTempC);
    queryPid(&ELM327::throttle,          out.throttlePct);
    queryPid(&ELM327::engineLoad,        out.engineLoadPct);
    queryPid(&ELM327::batteryVoltage,    out.batteryVoltage);
    queryPid(&ELM327::fuelLevel,         out.fuelLevelPct);

    float maf;
    if (queryPid(&ELM327::mafRate, maf)) {
        out.mafGs       = maf;
        out.fuelRateLph = fuelRateLphFromMaf(maf, _diesel);
    }

    out.timestampMs = millis();
    _last = out;
    _status = "obd-ready";
    return true;
}
