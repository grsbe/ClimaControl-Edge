#pragma once
// The complete device configuration. Everything that may change between installations
// (pins, sensors, setpoints, schedules, credentials) lives here and is stored as JSON.

#include <cmath>
#include <string>
#include <vector>

#include "Pins.h"
#include "Types.h"

// Factory passwords are not stored in git. tools/env_defaults.py reads them from .env at build time.
#if !defined(CC_DEFAULT_AP_PASSWORD) || !defined(CC_DEFAULT_WEB_PASSWORD)
#error "Factory passwords missing: copy .env.example to .env (see tools/env_defaults.py)"
#endif

namespace cc {

// Passwords are reported as this string. Writing it back keeps the stored password.
extern const char* const kSecretMask;

struct SensorConfig {
    std::string id;
    SensorType type = SensorType::Bme280;
    std::string group;                      // sensors in the same group are averaged, e.g. "chamber"
    int muxChannel = -1;                    // TCA9548 channel 0..7, -1 = directly on the bus
    int address = 0x76;                     // 7-bit I2C address
    float offset[kQuantityCount] = {};      // calibration, added to every raw reading
};

enum class RuleType { Above, Below, Schedule, Cycle, Linear };

struct TimeWindow {
    int fromMin = 0;  // minute of the day, 0..1439
    int toMin = 0;    // exclusive; a window with to < from wraps over midnight
};

// A rule decides when an output should be active. An output is active if any of its rules is.
struct Rule {
    RuleType type = RuleType::Above;
    std::string signal;  // "group.quantity", e.g. "chamber.co2" (Above, Below, Linear)
    float on = 0;        // Above: on at >= on, off at <= off. Below: on at <= on, off at >= off.
    float off = 0;
    float level = 1.0f;  // output level while the rule is active (PWM duty 0..1)
    std::vector<TimeWindow> windows;  // Schedule
    uint32_t onS = 0;                 // Cycle: on for onS at the start of every periodS
    uint32_t periodS = 0;
    float inMin = 0, inMax = 1, outMin = 0, outMax = 1;  // Linear: map signal range to level range
};

enum class OutputKind { Relay, Pwm };

struct OutputConfig {
    std::string name;   // id used in API and MQTT topics: lowercase letters, digits, '-' and '_'
    std::string label;  // shown in the web UI
    int pin = -1;
    OutputKind kind = OutputKind::Relay;
    bool activeHigh = true;
    uint32_t pwmFreqHz = 1000;
    float failsafe = 0;     // level used while a rule's input is unavailable (sensor fault, no time)
    uint32_t minOnS = 0;    // protect devices from short cycling (0 = off)
    uint32_t minOffS = 0;
    uint32_t maxOnS = 0;    // force off after running this long, e.g. a humidifier stuck on (0 = off)
    uint32_t cooldownS = 300;  // how long to stay off after maxOnS tripped
    std::vector<Rule> rules;
};

struct AlarmConfig {
    std::string signal;
    float min = NAN;  // alarm if below (NaN = no lower limit)
    float max = NAN;
    uint32_t delayS = 300;  // must be out of range this long before the alarm fires
};

// Web/HTTP logins. Operators may switch outputs and change setpoints (see onlySetpointsChanged);
// admins may change everything, including pins, users, firmware and reboot.
enum class Role { Admin, Operator };
const char* roleName(Role r);

struct WebUser {
    std::string name;
    std::string password;
    Role role = Role::Admin;
};

struct Config {
    std::string deviceName = "climacontrol";

    struct {
        bool apEnabled = true;
        std::string apSsid = "ClimaControl";
        std::string apPassword = CC_DEFAULT_AP_PASSWORD;
        std::string staSsid;
        std::string staPassword;
    } wifi;

    struct {
        std::vector<WebUser> users = {WebUser{"admin", CC_DEFAULT_WEB_PASSWORD, Role::Admin}};
    } web;

    struct {
        bool enabled = false;
        std::string host;
        int port = 1883;
        std::string user;
        std::string password;
        std::string baseTopic = "wildnis/climacontrol";
        uint32_t publishIntervalS = 10;
    } mqtt;

    struct {
        std::string tz = "CET-1CEST,M3.5.0,M10.5.0/3";  // POSIX TZ string, Europe/Berlin
        std::string ntpServer = "pool.ntp.org";
    } time;

    struct {
        int sda = 21;
        int scl = 22;
        int muxAddress = 0x70;
        uint32_t freqHz = 100000;
    } i2c;

    bool simulateSensors = false;
    uint32_t staleS = 30;  // readings older than this are ignored
    float tolerance[kQuantityCount] = {2.0f, 8.0f, 200.0f, 5.0f, 1e9f};  // max spread before "disagree"
    uint32_t overrideTimeoutS = 3600;  // manual on/off falls back to auto after this (0 = never)

    std::vector<SensorConfig> sensors;
    std::vector<OutputConfig> outputs;
    std::vector<AlarmConfig> alarms;
};

// Factory configuration matching the ClimaControl block diagram. Setpoints are placeholders.
Config defaultConfig(Chip chip);

// Overlays a JSON document onto cfg: fields that are missing keep their current value, arrays
// (sensors, outputs, alarms) are replaced as a whole. Returns false and fills errors if the
// JSON is malformed or has wrongly typed fields; cfg may then be partially modified.
bool parseConfig(const std::string& json, Config& cfg, std::vector<std::string>& errors);

// Semantic checks (unique names, safe pins, signals that exist, sane thresholds).
std::vector<std::string> validateConfig(const Config& cfg, Chip chip);

std::string configToJson(const Config& cfg, bool includeSecrets);

// The user with this name and password, or nullptr.
const WebUser* findUser(const Config& cfg, const std::string& name, const std::string& password);

// True if `after` differs from `before` only in setpoints: rule thresholds, levels, time windows,
// cycle times, linear ranges, minOnS/minOffS, and alarm limits/delays. Everything else (pins, rule
// types, signals, maxOnS, users, network, ...) must be identical. Used for the operator role.
bool onlySetpointsChanged(const Config& before, const Config& after);

// Human-readable list of differences, e.g. "outputs[humidifier].rules[0].on: 85 -> 83".
// Passwords are never shown, only reported as changed.
std::vector<std::string> describeChanges(const Config& before, const Config& after);

// Splits "chamber.co2" into group and quantity.
bool parseSignalName(const std::string& name, std::string& group, Quantity& q);

}  // namespace cc
