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

// Status topic — device publishes connection/OBD state here (watch from phone).
#define MQTT_STATUS_TOPIC       "smartcar/status"

// Config topic — publish {"preset":"fuel-health"} (retained) here to change what
// the device logs at runtime, no re-flash. Presets: standard | fuel-health | full.
#define MQTT_CONFIG_TOPIC       "smartcar/config"
#define LOG_PRESET              "standard"

// ---- Telemetry source -------------------------------------------------------
// 0 = simulator (no car needed), 1 = real ELM327 over Bluetooth.
#define USE_ELM327           0
#define OBD_BT_NAME          "OBDII"   // your ELM327's Bluetooth name
#define OBD_BT_MAC           ""        // e.g. "1A:2B:3C:4D:5E:6F" — if set, connect by MAC; "" = by name
#define OBD_BT_PIN           "1234"    // legacy pairing PIN for cheap ELM327 clones
#define FUEL_IS_DIESEL       1         // 1 = diesel, 0 = petrol (fuel-from-MAF math)

// ---- App --------------------------------------------------------------------
#define DEVICE_ID            "car-01"
#define PUBLISH_INTERVAL_MS  1000      // how often to publish a reading
