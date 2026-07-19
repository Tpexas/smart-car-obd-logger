#include <Arduino.h>
#include <WiFi.h>
#include <time.h>
#include <esp_system.h>   // esp_reset_reason()

#include "config.h"
#include "ca_cert.h"
#include "LogConfig.h"
#include "ITelemetrySource.h"
#if USE_ELM327
  #include "ELM327Source.h"
#else
  #include "SimulatorSource.h"
#endif
#include "MqttPublisher.h"

// Which signals we log — chosen at runtime over MQTT (smartcar/config). Shared by
// the source (polls only active PIDs) and the publisher (emits only active fields).
LogConfig gConfig;

// --- The telemetry source -----------------------------------------------------
// Today: the simulator. When the car is available, write ELM327Source (it just
// has to implement ITelemetrySource) and change this one line. Nothing below
// needs to know which source it is.
#if USE_ELM327
ELM327Source       source(OBD_BT_NAME, OBD_BT_MAC, OBD_BT_PIN, FUEL_IS_DIESEL);
#else
SimulatorSource    source;
#endif
ITelemetrySource&  telemetry = source;

MqttPublisher mqtt(MQTT_HOST, MQTT_PORT, MQTT_CLIENT_ID,
                   MQTT_USERNAME, MQTT_PASSWORD, MQTT_TELEMETRY_TOPIC);

uint32_t lastPublishMs = 0;

// Trip identity, kept in RTC memory so it survives a self-heal reboot (a recovery
// restart should continue the SAME trip). Cleared only on a real power-on.
RTC_DATA_ATTR char     rtcTripId[24];
RTC_DATA_ATTR uint32_t rtcTripMagic;

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

    // One trip = one power-on session. A fresh power-on (plugging into the car)
    // starts a new trip; a self-heal reboot (software reset) keeps the same trip_id
    // from RTC memory so the drive stays a single trip.
    bool newTrip = (esp_reset_reason() == ESP_RST_POWERON) || (rtcTripMagic != 0xC0FFEEu);
    if (newTrip) {
        snprintf(rtcTripId, sizeof(rtcTripId), "trip-%ld", (long)time(nullptr));
        rtcTripMagic = 0xC0FFEEu;
        Serial.printf("[Trip] new id = %s\n", rtcTripId);
    } else {
        Serial.printf("[Trip] resumed id = %s (after recovery reboot)\n", rtcTripId);
    }
    mqtt.setTripId(rtcTripId);

    mqtt.setStatusTopic(MQTT_STATUS_TOPIC);

    // Runtime logging selection: default preset now, then reconfigured live by any
    // retained message on smartcar/config.
    gConfig.setPreset(LOG_PRESET);
    mqtt.setConfig(&gConfig, MQTT_CONFIG_TOPIC);
    telemetry.setConfig(&gConfig);
    Serial.printf("[Config] default preset: %s\n", gConfig.presetName);

#if MQTT_TLS_INSECURE
    mqtt.begin(nullptr);            // skip cert verification (debug)
#else
    mqtt.begin(ROOT_CA_BUNDLE);     // verify broker against Let's Encrypt roots
#endif
    // NOTE: the telemetry source is started later, in loop(), *after* MQTT is up.
    // For ELM327 that start is a blocking Bluetooth connect — doing it here would
    // (and did) prevent us from ever reaching the broker if the adapter is missing
    // or slow. Starting it after MQTT also means its status is visible remotely.
}

void loop() {
    mqtt.loop();   // keeps MQTT alive / reconnects

    // Self-heal: BT-Classic + WiFi + TLS is memory-tight on the WROOM, so once
    // Bluetooth is up a dropped MQTT link may fail to re-handshake (no contiguous
    // heap). If MQTT stays down too long, reboot — a fresh boot has full heap and
    // reconnects, and the trip_id is preserved in RTC so the trip continues.
    // Guarded by everConnected so the slow first connect at boot never triggers it.
    static uint32_t mqttDownSince = 0;
    static bool everConnected = false;
    if (mqtt.connected()) {
        everConnected = true;
        mqttDownSince = 0;
    } else if (everConnected) {
        if (mqttDownSince == 0) {
            mqttDownSince = millis();
        } else if (millis() - mqttDownSince > 45000) {
            Serial.println("[SelfHeal] MQTT down >45s after BT — restarting to recover memory");
            delay(100);
            ESP.restart();
        }
    }

    // Publish device/OBD status whenever it changes, so you can watch the car
    // test from your phone/server with no serial cable.
    static const char* lastStatus = "";
    static bool wasConnected = false;
    static bool sourceStarted = false;

    if (mqtt.connected()) {
        if (!wasConnected) {
            mqtt.publishStatus("online");   // proves MQTT works, BEFORE any OBD connect
            wasConnected = true;
            lastStatus = "online";
            Serial.printf("[Heap] after MQTT connect (pre-BT): %u bytes free\n", ESP.getFreeHeap());
        }
        // Start the telemetry source once, only after MQTT is up. For ELM327 this is
        // the blocking Bluetooth connect — keeping it here means a missing/slow
        // adapter can never stop us reaching the broker.
        if (!sourceStarted) {
            if (!telemetry.begin()) Serial.println("[Source] begin() failed!");
            sourceStarted = true;
            const char* dg = telemetry.diag();
            if (dg[0]) mqtt.publishDiag(dg);   // BT scan → its own retained topic (smartcar/scan)
        }
        const char* st = telemetry.statusText();
        if (st != lastStatus) {       // string literals → stable pointers
            mqtt.publishStatus(st);
            lastStatus = st;
        }
    } else {
        wasConnected = false;
    }

    if (sourceStarted && millis() - lastPublishMs >= PUBLISH_INTERVAL_MS) {
        lastPublishMs = millis();

        TelemetrySnapshot snap;
        if (telemetry.read(snap)) {
            mqtt.publish(snap);   // no-op (returns false) if MQTT is down
        }
    }
}
