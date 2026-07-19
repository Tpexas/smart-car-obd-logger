#pragma once
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include "TelemetrySnapshot.h"
#include "LogConfig.h"

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

    // Topic for device/OBD status messages (retained, so the last state is visible).
    void setStatusTopic(const char* topic) { _statusTopic = topic; }
    bool publishStatus(const char* state);
    // Bluetooth scan / pairing diagnostic — its OWN retained topic so a later status
    // message can't overwrite it.
    bool publishDiag(const char* text);

    // Runtime logging config: the device subscribes to `configTopic` and reconfigures
    // which signals it logs when a message arrives — no re-flash.
    void setConfig(LogConfig* cfg, const char* configTopic) { _cfg = cfg; _configTopic = configTopic; }

private:
    bool reconnect();
    void handleConfig(const char* payload);
    static void onMqttMessage(char* topic, uint8_t* payload, unsigned int len);
    static MqttPublisher* s_self;

    WiFiClientSecure _net;
    PubSubClient     _client;

    LogConfig*  _cfg = nullptr;
    const char* _configTopic = nullptr;

    const char* _host;
    uint16_t    _port;
    const char* _clientId;
    const char* _username;
    const char* _password;
    const char* _topic;
    const char* _tripId = "";
    const char* _statusTopic = nullptr;

    uint32_t _lastReconnectMs = 0;
    uint32_t _backoffMs       = 1000;   // grows up to a cap on repeated failures
};
