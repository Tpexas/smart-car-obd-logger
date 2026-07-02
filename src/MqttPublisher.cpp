#include "MqttPublisher.h"
#include <ArduinoJson.h>
#include <time.h>

MqttPublisher::MqttPublisher(const char* host, uint16_t port,
                             const char* clientId,
                             const char* username, const char* password,
                             const char* telemetryTopic)
    : _client(_net),
      _host(host), _port(port), _clientId(clientId),
      _username(username), _password(password), _topic(telemetryTopic) {}

void MqttPublisher::begin(const char* caCert) {
    if (caCert && strlen(caCert) > 0) {
        _net.setCACert(caCert);   // verify the broker against this root CA
    } else {
        _net.setInsecure();       // skip verification (dev/debug only)
        Serial.println("[MQTT] WARNING: TLS verification disabled (insecure mode)");
    }
    _client.setServer(_host, _port);
    _client.setBufferSize(512);   // headroom for the JSON payload
    // Longer keepalive so brief Bluetooth-induced loop stalls don't drop the
    // connection — reconnecting is expensive (and memory-starved) once BT is up.
    _client.setKeepAlive(60);
    _client.setSocketTimeout(10);
}

bool MqttPublisher::reconnect() {
    uint32_t now = millis();
    if (now - _lastReconnectMs < _backoffMs) return false;
    _lastReconnectMs = now;

    Serial.printf("[MQTT] connecting to %s:%u ... ", _host, _port);

    // Empty username string -> connect without credentials.
    bool ok;
    if (_username && strlen(_username) > 0) {
        ok = _client.connect(_clientId, _username, _password);
    } else {
        ok = _client.connect(_clientId);
    }

    if (ok) {
        Serial.println("connected");
        _backoffMs = 1000;        // reset backoff on success
        return true;
    }

    Serial.printf("failed (state=%d), retrying in %lus\n",
                  _client.state(), (unsigned long)(_backoffMs / 1000));
    uint32_t next = _backoffMs * 2;                      // cap at 30s
    _backoffMs = next > 30000 ? 30000 : next;
    return false;
}

void MqttPublisher::loop() {
    if (!_client.connected()) {
        reconnect();
    }
    _client.loop();
}

bool MqttPublisher::connected() {
    return _client.connected();
}

bool MqttPublisher::publish(const TelemetrySnapshot& s) {
    if (!_client.connected()) return false;

    // Flat JSON of key/value telemetry — this is exactly the shape ThingsBoard
    // expects on its telemetry topic, and it's trivial to parse in Node-RED.
    JsonDocument doc;
    // Trip/session identity + real wall-clock time. The device timestamp (not the
    // server's arrival time) is what makes buffered/offline data line up correctly
    // later — see Phase 3.
    doc["trip_id"]  = _tripId;
    doc["ts"]       = (uint64_t)time(nullptr) * 1000ULL;   // epoch milliseconds
    doc["rpm"]      = (int)(s.rpm + 0.5f);
    doc["speed"]    = round(s.speedKph * 10) / 10.0;
    doc["coolant"]  = round(s.coolantTempC * 10) / 10.0;
    doc["intake"]   = round(s.intakeTempC * 10) / 10.0;
    doc["throttle"] = round(s.throttlePct * 10) / 10.0;
    doc["load"]     = round(s.engineLoadPct * 10) / 10.0;
    doc["voltage"]  = round(s.batteryVoltage * 100) / 100.0;
    doc["fuel"]     = round(s.fuelLevelPct * 10) / 10.0;
    doc["maf"]      = round(s.mafGs * 100) / 100.0;
    doc["fuel_rate"]= round(s.fuelRateLph * 100) / 100.0;

    char payload[512];
    size_t n = serializeJson(doc, payload, sizeof(payload));

    bool ok = _client.publish(_topic, (const uint8_t*)payload, n, false);
    Serial.printf("[MQTT] %s -> %s : %s\n", ok ? "pub" : "PUB-FAIL", _topic, payload);
    return ok;
}

bool MqttPublisher::publishStatus(const char* state) {
    if (!_client.connected() || !_statusTopic) return false;

    JsonDocument doc;
    doc["trip_id"] = _tripId;
    doc["ts"]      = (uint64_t)time(nullptr) * 1000ULL;
    doc["state"]   = state;

    char payload[256];
    size_t n = serializeJson(doc, payload, sizeof(payload));
    bool ok = _client.publish(_statusTopic, (const uint8_t*)payload, n, true);   // retained
    Serial.printf("[MQTT] status -> %s : %s\n", _statusTopic, payload);
    return ok;
}

bool MqttPublisher::publishDiag(const char* text) {
    if (!_client.connected()) return false;

    JsonDocument doc;
    doc["trip_id"] = _tripId;
    doc["ts"]      = (uint64_t)time(nullptr) * 1000ULL;
    doc["scan"]    = text;

    char payload[512];
    size_t n = serializeJson(doc, payload, sizeof(payload));
    bool ok = _client.publish("smartcar/scan", (const uint8_t*)payload, n, true);   // own retained topic
    Serial.printf("[MQTT] scan -> smartcar/scan : %s\n", payload);
    return ok;
}
