#pragma once
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include "TelemetrySnapshot.h"

// Owns the MQTT connection and turns a TelemetrySnapshot into a JSON message.
// Non-blocking: call loop() often; it reconnects in the background with backoff.
class MqttPublisher {
public:
    MqttPublisher(const char* host, uint16_t port,
                  const char* clientId,
                  const char* username, const char* password,
                  const char* telemetryTopic);

    // caCert: PEM root(s) to verify the broker. Pass nullptr to skip verification
    // (insecure — dev/debug only).
    void begin(const char* caCert);
    void loop();                 // keep the connection alive; call every iteration
    bool connected();
    bool publish(const TelemetrySnapshot& s);

    // Identifies the current trip/session (one per power-on). Stamped on every message.
    void setTripId(const char* tripId) { _tripId = tripId; }

private:
    bool reconnect();

    WiFiClientSecure _net;
    PubSubClient     _client;

    const char* _host;
    uint16_t    _port;
    const char* _clientId;
    const char* _username;
    const char* _password;
    const char* _topic;
    const char* _tripId = "";

    uint32_t _lastReconnectMs = 0;
    uint32_t _backoffMs       = 1000;   // grows up to a cap on repeated failures
};
