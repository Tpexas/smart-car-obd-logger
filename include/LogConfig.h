#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>

// ---------------------------------------------------------------------------
// Runtime-selectable logging.
//
// Which signals the device logs is chosen at RUNTIME (over MQTT), not baked in.
// Publish a retained message to the config topic and the device reconfigures —
// no re-flash:
//     {"preset":"fuel-health"}                     // a named preset
//     {"signals":["rpm","speed","rail_pressure"]}  // an explicit list
//
// To add a brand-new signal: add a bit here, a row in SIGNALS[], read it in
// ELM327Source, and emit it in MqttPublisher. After that one flash it's
// runtime-selectable forever.
// ---------------------------------------------------------------------------

enum Signal : uint32_t {
    SIG_RPM       = 1u << 0,
    SIG_SPEED     = 1u << 1,
    SIG_COOLANT   = 1u << 2,
    SIG_INTAKE    = 1u << 3,
    SIG_THROTTLE  = 1u << 4,
    SIG_LOAD      = 1u << 5,
    SIG_VOLTAGE   = 1u << 6,
    SIG_FUEL      = 1u << 7,
    SIG_MAF       = 1u << 8,
    SIG_FUEL_RATE = 1u << 9,
    SIG_RAIL      = 1u << 10,   // fuel rail gauge pressure (PID 0123) — CP4 pump health
    SIG_MAP       = 1u << 11,   // intake manifold absolute pressure (PID 010B) — boost/turbo health
};

struct SigDef { uint32_t bit; const char* key; };

static const SigDef SIGNALS[] = {
    { SIG_RPM,       "rpm"       },
    { SIG_SPEED,     "speed"     },
    { SIG_COOLANT,   "coolant"   },
    { SIG_INTAKE,    "intake"    },
    { SIG_THROTTLE,  "throttle"  },
    { SIG_LOAD,      "load"      },
    { SIG_VOLTAGE,   "voltage"   },
    { SIG_FUEL,      "fuel"      },
    { SIG_MAF,       "maf"       },
    { SIG_FUEL_RATE, "fuel_rate" },
    { SIG_RAIL,      "rail_pressure" },
    { SIG_MAP,       "map" },
};
static const size_t SIGNAL_COUNT = sizeof(SIGNALS) / sizeof(SIGNALS[0]);

// Named presets.
static const uint32_t PRESET_STANDARD =
    SIG_RPM | SIG_SPEED | SIG_COOLANT | SIG_INTAKE | SIG_THROTTLE |
    SIG_LOAD | SIG_VOLTAGE | SIG_FUEL | SIG_MAF | SIG_FUEL_RATE;
// Fuel-system health watch (CP4 / injectors / turbo): keeps rpm+speed for trip
// logic and voltage/coolant for the health trends, adds rail pressure + boost
// (MAP), drops the rest. MAF+fuel_rate together also give AFR (smoke limit).
static const uint32_t PRESET_FUEL_HEALTH =
    SIG_RPM | SIG_SPEED | SIG_LOAD | SIG_COOLANT | SIG_VOLTAGE |
    SIG_MAF | SIG_FUEL_RATE | SIG_RAIL | SIG_MAP;
static const uint32_t PRESET_FULL = 0xFFFFFFFFu;

inline uint32_t maskFromPreset(const char* name) {
    if (!strcmp(name, "fuel-health")) return PRESET_FUEL_HEALTH;
    if (!strcmp(name, "full"))        return PRESET_FULL;
    return PRESET_STANDARD;   // default / "standard"
}

inline uint32_t bitForKey(const char* key) {
    if (!key) return 0;   // non-string JSON entries yield nullptr — never strcmp it
    for (size_t i = 0; i < SIGNAL_COUNT; i++)
        if (!strcmp(SIGNALS[i].key, key)) return SIGNALS[i].bit;
    return 0;
}

class LogConfig {
public:
    uint32_t active = PRESET_STANDARD;
    char     presetName[16] = "standard";

    bool isActive(uint32_t sig) const { return (active & sig) != 0; }

    void setPreset(const char* name) {
        active = maskFromPreset(name);
        // Store the canonical name: unknown presets fall back to the standard
        // mask, so report "standard" rather than echoing an unknown label.
        bool known = !strcmp(name, "fuel-health") || !strcmp(name, "full") || !strcmp(name, "standard");
        strlcpy(presetName, known ? name : "standard", sizeof(presetName));
    }

    // Apply an MQTT config payload. Returns false on unparseable input.
    bool applyJson(const char* json) {
        JsonDocument doc;
        if (deserializeJson(doc, json)) return false;

        if (doc["preset"].is<const char*>()) {
            setPreset(doc["preset"].as<const char*>());
            return true;
        }
        if (doc["signals"].is<JsonArray>()) {
            uint32_t m = 0;
            for (JsonVariant v : doc["signals"].as<JsonArray>())
                m |= bitForKey(v.as<const char*>());
            if (!m) return false;
            active = m | SIG_RPM | SIG_SPEED;   // always keep rpm+speed (trip logic)
            strlcpy(presetName, "custom", sizeof(presetName));
            return true;
        }
        return false;
    }
};
