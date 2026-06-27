#pragma once
//
// Copy this file to "config.h" (same folder) and fill in your real values.
// config.h is git-ignored so your secrets never end up on your portfolio repo.
//
//   cp include/config.example.h include/config.h
//

// ---- WiFi -------------------------------------------------------------------
// For now use your home WiFi or your phone's hotspot. In the car it'll be the
// phone hotspot providing the 4G uplink.
#define WIFI_SSID       "your-wifi-or-hotspot"
#define WIFI_PASSWORD   "your-wifi-password"

// ---- MQTT broker (TLS) ------------------------------------------------------
// Use the broker HOSTNAME (not an IP) — the TLS cert is verified against it.
// Port 8883 = MQTT over TLS. The firmware trusts Let's Encrypt roots (ca_cert.h);
// if your broker uses a different CA, replace the certs in ca_cert.h.
#define MQTT_HOST       "mqtt.your-domain.com"
#define MQTT_PORT       8883
#define MQTT_CLIENT_ID  "smartcar-esp32"

// Broker credentials (Mosquitto password_file user).
#define MQTT_USERNAME           "your-broker-user"
#define MQTT_PASSWORD           "your-broker-password"
#define MQTT_TELEMETRY_TOPIC    "smartcar/telemetry"

// Set to 1 to skip TLS certificate verification (debug only). Leave 0 normally.
#define MQTT_TLS_INSECURE       0

// ---- App --------------------------------------------------------------------
#define DEVICE_ID            "car-01"
#define PUBLISH_INTERVAL_MS  1000      // how often to publish a reading
