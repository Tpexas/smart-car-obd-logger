#include <Arduino.h>
#include <WiFi.h>
#include <time.h>

#include "config.h"
#include "ca_cert.h"
#include "ITelemetrySource.h"
#include "SimulatorSource.h"
#include "MqttPublisher.h"

// --- The telemetry source -----------------------------------------------------
// Today: the simulator. When the car is available, write ELM327Source (it just
// has to implement ITelemetrySource) and change this one line. Nothing below
// needs to know which source it is.
SimulatorSource    source;
ITelemetrySource&  telemetry = source;

MqttPublisher mqtt(MQTT_HOST, MQTT_PORT, MQTT_CLIENT_ID,
                   MQTT_USERNAME, MQTT_PASSWORD, MQTT_TELEMETRY_TOPIC);

uint32_t lastPublishMs = 0;

// Prints the 2.4 GHz networks the ESP32 can actually see. If your target SSID is
// not in this list, the ESP32 cannot reach it (commonly because it's a 5 GHz
// network — the ESP32 only does 2.4 GHz).
void scanNetworks() {
    Serial.println("[WiFi] scanning for visible 2.4 GHz networks...");
    int n = WiFi.scanNetworks();
    if (n <= 0) {
        Serial.println("[WiFi]   (none found)");
        return;
    }
    for (int i = 0; i < n; ++i) {
        Serial.printf("[WiFi]   %2d  %4d dBm  ch%-2d  %s\n",
                      i, WiFi.RSSI(i), WiFi.channel(i), WiFi.SSID(i).c_str());
    }
    WiFi.scanDelete();
}

void connectWifi() {
    WiFi.mode(WIFI_STA);
    scanNetworks();

    Serial.printf("[WiFi] connecting to \"%s\" ", WIFI_SSID);
    WiFi.setAutoReconnect(true);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    uint32_t start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < 20000) {
        delay(300);
        Serial.print(".");
    }
    if (WiFi.status() == WL_CONNECTED) {
        Serial.printf(" connected, IP=%s\n", WiFi.localIP().toString().c_str());
    } else {
        Serial.println(" FAILED (will keep retrying in background)");
    }
}

// TLS certificate validation checks the cert's validity dates, so the ESP32 must
// know the real time. It boots at 1970, so we sync via NTP before connecting —
// otherwise every TLS handshake fails with "cert not yet valid".
void syncTime() {
    configTime(0, 0, "pool.ntp.org", "time.google.com");
    Serial.print("[Time] syncing via NTP ");
    time_t now = 0;
    uint32_t start = millis();
    while (now < 1700000000 && millis() - start < 15000) {   // ~2023-11 sanity floor
        delay(300);
        Serial.print(".");
        now = time(nullptr);
    }
    if (now >= 1700000000) {
        Serial.printf(" ok (%s", ctime(&now));   // ctime ends with '\n'
        Serial.println(")");
    } else {
        Serial.println(" FAILED — TLS verification will likely fail");
    }
}

void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("\n=== Smart Car telemetry ===");
    Serial.printf("Device: %s | Source: %s\n", DEVICE_ID, telemetry.name());

    connectWifi();
    syncTime();

    if (!telemetry.begin()) {
        Serial.println("[Source] begin() failed!");
    }

#if MQTT_TLS_INSECURE
    mqtt.begin(nullptr);            // skip cert verification (debug)
#else
    mqtt.begin(ROOT_CA_BUNDLE);     // verify broker against Let's Encrypt roots
#endif
}

void loop() {
    mqtt.loop();   // keeps MQTT alive / reconnects

    uint32_t now = millis();
    if (now - lastPublishMs >= PUBLISH_INTERVAL_MS) {
        lastPublishMs = now;

        TelemetrySnapshot snap;
        if (telemetry.read(snap)) {
            mqtt.publish(snap);   // no-op (returns false) if MQTT is down
        }
    }
}
