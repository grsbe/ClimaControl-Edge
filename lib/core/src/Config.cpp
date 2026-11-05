#include "Config.h"

#include <ArduinoJson.h>

#include <cstdio>
#include <set>
#include <utility>

namespace cc {

const char* const kSecretMask = "********";

namespace {

const char* ruleTypeName(RuleType t) {
    switch (t) {
        case RuleType::Above: return "above";
        case RuleType::Below: return "below";
        case RuleType::Schedule: return "schedule";
        case RuleType::Cycle: return "cycle";
        case RuleType::Linear: return "linear";
    }
    return "?";
}

bool ruleTypeFromName(const std::string& name, RuleType& out) {
    for (RuleType t : {RuleType::Above, RuleType::Below, RuleType::Schedule, RuleType::Cycle, RuleType::Linear}) {
        if (name == ruleTypeName(t)) {
            out = t;
            return true;
        }
    }
    return false;
}

std::string formatTime(int minutes) {
    char buf[16];
    snprintf(buf, sizeof buf, "%02d:%02d", minutes / 60, minutes % 60);
    return buf;
}

// "HH:MM", 00:00 .. 24:00
bool parseTime(const std::string& s, int& out) {
    int h, m;
    char extra;
    if (sscanf(s.c_str(), "%d:%d%c", &h, &m, &extra) != 2) return false;
    if (h < 0 || h > 24 || m < 0 || m > 59 || (h == 24 && m != 0)) return false;
    out = h * 60 + m;
    return true;
}

bool validName(const std::string& s) {
    if (s.empty() || s.size() > 32) return false;
    for (char c : s) {
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-')) return false;
    }
    return true;
}

int defaultAddress(SensorType t) {
    switch (t) {
        case SensorType::Scd41: return 0x62;
        case SensorType::Bme680: return 0x77;
        case SensorType::Bme280: return 0x76;
    }
    return 0;
}

// Reads typed fields, records an error for every field that is present but has the wrong type.
class Reader {
   public:
    explicit Reader(std::vector<std::string>& errors) : errors_(errors) {}

    void fail(const std::string& path, const char* msg) { errors_.push_back(path + ": " + msg); }

    template <typename T>
    void integer(JsonVariantConst v, const std::string& path, T& out) {
        if (v.isNull()) return;
        if (!v.is<T>()) return fail(path, "expected a whole number in range");
        out = v.as<T>();
    }

    void number(JsonVariantConst v, const std::string& path, float& out) {
        if (v.isNull()) return;
        if (!v.is<float>()) return fail(path, "expected a number");
        out = v.as<float>();
    }

    void boolean(JsonVariantConst v, const std::string& path, bool& out) {
        if (v.isNull()) return;
        if (!v.is<bool>()) return fail(path, "expected true or false");
        out = v.as<bool>();
    }

    void string(JsonVariantConst v, const std::string& path, std::string& out) {
        if (v.isNull()) return;
        if (!v.is<const char*>()) return fail(path, "expected a string");
        out = v.as<const char*>();
    }

    // Like string(), but the mask placeholder keeps the current value.
    void secret(JsonVariantConst v, const std::string& path, std::string& out) {
        std::string s = out;
        string(v, path, s);
        if (s != kSecretMask) out = s;
    }

    // Accepts true/false (= 1/0) or a level 0..1.
    void level(JsonVariantConst v, const std::string& path, float& out) {
        if (v.is<bool>()) {
            out = v.as<bool>() ? 1.0f : 0.0f;
        } else {
            number(v, path, out);
        }
    }

    void pair(JsonVariantConst v, const std::string& path, float& a, float& b) {
        if (v.isNull()) return;
        JsonArrayConst arr = v.as<JsonArrayConst>();
        if (arr.isNull() || arr.size() != 2 || !arr[0].is<float>() || !arr[1].is<float>())
            return fail(path, "expected [from, to]");
        a = arr[0].as<float>();
        b = arr[1].as<float>();
    }

    bool array(JsonVariantConst v, const std::string& path) {
        if (v.isNull()) return false;
        if (!v.is<JsonArrayConst>()) {
            fail(path, "expected an array");
            return false;
        }
        return true;
    }

    void quantityMap(JsonVariantConst v, const std::string& path, float (&out)[kQuantityCount]) {
        if (v.isNull()) return;
        if (!v.is<JsonObjectConst>()) return fail(path, "expected an object");
        for (JsonPairConst kv : v.as<JsonObjectConst>()) {
            std::string key = kv.key().c_str();
            Quantity q;
            if (!quantityFromName(key, q)) {
                fail(path + "." + key, "unknown quantity");
                continue;
            }
            number(kv.value(), path + "." + key, out[idx(q)]);
        }
    }

   private:
    std::vector<std::string>& errors_;
};

void readRule(Reader& r, JsonVariantConst e, const std::string& p, Rule& rule) {
    std::string type;
    r.string(e["type"], p + ".type", type);
    if (!ruleTypeFromName(type, rule.type)) r.fail(p + ".type", "must be above, below, schedule, cycle or linear");
    r.string(e["signal"], p + ".signal", rule.signal);
    r.number(e["on"], p + ".on", rule.on);
    r.number(e["off"], p + ".off", rule.off);
    r.level(e["level"], p + ".level", rule.level);
    r.integer(e["onS"], p + ".onS", rule.onS);
    r.integer(e["periodS"], p + ".periodS", rule.periodS);
    r.pair(e["in"], p + ".in", rule.inMin, rule.inMax);
    r.pair(e["out"], p + ".out", rule.outMin, rule.outMax);
    if (r.array(e["windows"], p + ".windows")) {
        int i = 0;
        for (JsonVariantConst w : e["windows"].as<JsonArrayConst>()) {
            std::string wp = p + ".windows[" + std::to_string(i++) + "]";
            std::string from, to;
            r.string(w["from"], wp + ".from", from);
            r.string(w["to"], wp + ".to", to);
            TimeWindow tw;
            if (!parseTime(from, tw.fromMin) || !parseTime(to, tw.toMin)) {
                r.fail(wp, "expected {\"from\": \"HH:MM\", \"to\": \"HH:MM\"}");
                continue;
            }
            rule.windows.push_back(tw);
        }
    }
}

void readOutput(Reader& r, JsonVariantConst e, const std::string& p, OutputConfig& o) {
    r.string(e["name"], p + ".name", o.name);
    r.string(e["label"], p + ".label", o.label);
    r.integer(e["pin"], p + ".pin", o.pin);
    std::string kind = "relay";
    r.string(e["kind"], p + ".kind", kind);
    if (kind == "relay") {
        o.kind = OutputKind::Relay;
    } else if (kind == "pwm") {
        o.kind = OutputKind::Pwm;
    } else {
        r.fail(p + ".kind", "must be relay or pwm");
    }
    r.boolean(e["activeHigh"], p + ".activeHigh", o.activeHigh);
    r.integer(e["pwmFreqHz"], p + ".pwmFreqHz", o.pwmFreqHz);
    r.level(e["failsafe"], p + ".failsafe", o.failsafe);
    r.integer(e["minOnS"], p + ".minOnS", o.minOnS);
    r.integer(e["minOffS"], p + ".minOffS", o.minOffS);
    r.integer(e["maxOnS"], p + ".maxOnS", o.maxOnS);
    r.integer(e["cooldownS"], p + ".cooldownS", o.cooldownS);
    if (r.array(e["rules"], p + ".rules")) {
        int i = 0;
        for (JsonVariantConst re : e["rules"].as<JsonArrayConst>()) {
            Rule rule;
            readRule(r, re, p + ".rules[" + std::to_string(i++) + "]", rule);
            o.rules.push_back(rule);
        }
    }
}

bool signalExists(const Config& cfg, const std::string& name) {
    std::string group;
    Quantity q;
    if (!parseSignalName(name, group, q)) return false;
    for (const SensorConfig& s : cfg.sensors) {
        if (s.group == group && sensorProvides(s.type, q)) return true;
    }
    return false;
}

}  // namespace

const char* roleName(Role r) { return r == Role::Admin ? "admin" : "operator"; }

const WebUser* findUser(const Config& cfg, const std::string& name, const std::string& password) {
    for (const WebUser& u : cfg.web.users) {
        if (u.name == name && !u.password.empty() && u.password == password) return &u;
    }
    return nullptr;
}

bool onlySetpointsChanged(const Config& before, const Config& after) {
    if (before.outputs.size() != after.outputs.size() || before.alarms.size() != after.alarms.size()) return false;
    // Copy the allowed fields from `after` into a copy of `before`; if that gives `after`, nothing else changed.
    Config merged = before;
    for (size_t i = 0; i < after.outputs.size(); i++) {
        OutputConfig& m = merged.outputs[i];
        const OutputConfig& a = after.outputs[i];
        if (m.name != a.name || m.rules.size() != a.rules.size()) return false;
        m.minOnS = a.minOnS;
        m.minOffS = a.minOffS;
        for (size_t j = 0; j < a.rules.size(); j++) {
            Rule& mr = m.rules[j];
            const Rule& ar = a.rules[j];
            if (mr.type != ar.type || mr.signal != ar.signal) return false;
            mr.on = ar.on;
            mr.off = ar.off;
            mr.level = ar.level;
            mr.windows = ar.windows;
            mr.onS = ar.onS;
            mr.periodS = ar.periodS;
            mr.inMin = ar.inMin;
            mr.inMax = ar.inMax;
            mr.outMin = ar.outMin;
            mr.outMax = ar.outMax;
        }
    }
    for (size_t i = 0; i < after.alarms.size(); i++) {
        if (merged.alarms[i].signal != after.alarms[i].signal) return false;
        merged.alarms[i].min = after.alarms[i].min;
        merged.alarms[i].max = after.alarms[i].max;
        merged.alarms[i].delayS = after.alarms[i].delayS;
    }
    return configToJson(merged, true) == configToJson(after, true);
}

namespace {

// Flattens a JSON document into "path" -> "value". Array elements are labelled by their name/id
// (outputs[humidifier], sensors[inflow], web.users[anna]) or, if they have none, by index.
void flatten(JsonVariantConst v, const std::string& path, std::vector<std::pair<std::string, std::string>>& out) {
    if (v.is<JsonObjectConst>()) {
        for (JsonPairConst kv : v.as<JsonObjectConst>()) {
            flatten(kv.value(), path.empty() ? kv.key().c_str() : path + "." + kv.key().c_str(), out);
        }
    } else if (v.is<JsonArrayConst>()) {
        int i = 0;
        for (JsonVariantConst e : v.as<JsonArrayConst>()) {
            std::string label = std::to_string(i++);
            if (e["name"].is<const char*>()) label = e["name"].as<const char*>();
            else if (e["id"].is<const char*>()) label = e["id"].as<const char*>();
            flatten(e, path + "[" + label + "]", out);
        }
    } else {
        std::string s;
        if (v.is<const char*>()) s = v.as<const char*>();
        else serializeJson(v, s);
        out.emplace_back(path, s);
    }
}

}  // namespace

std::vector<std::string> describeChanges(const Config& before, const Config& after) {
    std::vector<std::pair<std::string, std::string>> a, b;
    JsonDocument da, db;
    deserializeJson(da, configToJson(before, false));
    deserializeJson(db, configToJson(after, false));
    flatten(da.as<JsonVariantConst>(), "", a);
    flatten(db.as<JsonVariantConst>(), "", b);

    std::vector<std::string> changes;
    auto lookup = [](const std::vector<std::pair<std::string, std::string>>& list, const std::string& key,
                     std::string& value) {
        for (const auto& kv : list) {
            if (kv.first == key) {
                value = kv.second;
                return true;
            }
        }
        return false;
    };
    // Passwords appear masked in the JSON; they are compared separately below.
    auto isPassword = [](const std::string& path) {
        return path.size() >= 8 && path.compare(path.size() - 8, 8, "password") == 0;
    };
    for (const auto& kv : a) {
        std::string nv;
        if (isPassword(kv.first)) continue;
        if (!lookup(b, kv.first, nv)) changes.push_back(kv.first + ": " + kv.second + " -> (removed)");
        else if (nv != kv.second) changes.push_back(kv.first + ": " + kv.second + " -> " + nv);
    }
    for (const auto& kv : b) {
        std::string ov;
        if (!isPassword(kv.first) && !lookup(a, kv.first, ov)) changes.push_back(kv.first + ": (new) " + kv.second);
    }
    // Passwords are masked in the JSON above, so compare them directly.
    if (before.wifi.apPassword != after.wifi.apPassword) changes.push_back("wifi.ap.password: changed");
    if (before.wifi.staPassword != after.wifi.staPassword) changes.push_back("wifi.sta.password: changed");
    if (before.mqtt.password != after.mqtt.password) changes.push_back("mqtt.password: changed");
    for (const WebUser& nu : after.web.users) {
        for (const WebUser& ou : before.web.users) {
            if (ou.name == nu.name && ou.password != nu.password)
                changes.push_back("web.users[" + nu.name + "].password: changed");
        }
    }
    return changes;
}

bool parseSignalName(const std::string& name, std::string& group, Quantity& q) {
    size_t dot = name.find('.');
    if (dot == std::string::npos) return false;
    group = name.substr(0, dot);
    return quantityFromName(name.substr(dot + 1), q);
}

bool parseConfig(const std::string& json, Config& cfg, std::vector<std::string>& errors) {
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, json);
    if (err) {
        errors.push_back(std::string("invalid JSON: ") + err.c_str());
        return false;
    }
    if (!doc.is<JsonObjectConst>()) {
        errors.push_back("config must be a JSON object");
        return false;
    }
    JsonObjectConst root = doc.as<JsonObjectConst>();
    size_t errorsBefore = errors.size();
    Reader r(errors);

    r.string(root["device"]["name"], "device.name", cfg.deviceName);

    JsonVariantConst wifi = root["wifi"];
    r.boolean(wifi["ap"]["enabled"], "wifi.ap.enabled", cfg.wifi.apEnabled);
    r.string(wifi["ap"]["ssid"], "wifi.ap.ssid", cfg.wifi.apSsid);
    r.secret(wifi["ap"]["password"], "wifi.ap.password", cfg.wifi.apPassword);
    r.string(wifi["sta"]["ssid"], "wifi.sta.ssid", cfg.wifi.staSsid);
    r.secret(wifi["sta"]["password"], "wifi.sta.password", cfg.wifi.staPassword);

    JsonVariantConst web = root["web"];
    if (!web["user"].isNull() || !web["password"].isNull()) {
        // Old format with a single login (firmware <= 0.1.0): becomes the only admin user.
        WebUser u = cfg.web.users.empty() ? WebUser{"admin", "", Role::Admin} : cfg.web.users.front();
        r.string(web["user"], "web.user", u.name);
        r.secret(web["password"], "web.password", u.password);
        u.role = Role::Admin;
        cfg.web.users = {u};
    }
    if (r.array(web["users"], "web.users")) {
        std::vector<WebUser> users;
        int i = 0;
        for (JsonVariantConst e : web["users"].as<JsonArrayConst>()) {
            std::string p = "web.users[" + std::to_string(i++) + "]";
            WebUser u;
            r.string(e["name"], p + ".name", u.name);
            // A masked password keeps the stored password of the user with the same name.
            for (const WebUser& old : cfg.web.users) {
                if (old.name == u.name) u.password = old.password;
            }
            const std::string before = u.password;
            r.secret(e["password"], p + ".password", u.password);
            if (u.password.empty() && before.empty()) r.fail(p + ".password", "set a password for this new user");
            std::string role = "operator";
            r.string(e["role"], p + ".role", role);
            if (role == "admin") u.role = Role::Admin;
            else if (role == "operator") u.role = Role::Operator;
            else r.fail(p + ".role", "must be admin or operator");
            users.push_back(u);
        }
        cfg.web.users = users;
    }

    JsonVariantConst mqtt = root["mqtt"];
    r.boolean(mqtt["enabled"], "mqtt.enabled", cfg.mqtt.enabled);
    r.string(mqtt["host"], "mqtt.host", cfg.mqtt.host);
    r.integer(mqtt["port"], "mqtt.port", cfg.mqtt.port);
    r.string(mqtt["user"], "mqtt.user", cfg.mqtt.user);
    r.secret(mqtt["password"], "mqtt.password", cfg.mqtt.password);
    r.string(mqtt["baseTopic"], "mqtt.baseTopic", cfg.mqtt.baseTopic);
    r.integer(mqtt["publishIntervalS"], "mqtt.publishIntervalS", cfg.mqtt.publishIntervalS);

    r.string(root["time"]["tz"], "time.tz", cfg.time.tz);
    r.string(root["time"]["ntp"], "time.ntp", cfg.time.ntpServer);

    JsonVariantConst i2c = root["i2c"];
    r.integer(i2c["sda"], "i2c.sda", cfg.i2c.sda);
    r.integer(i2c["scl"], "i2c.scl", cfg.i2c.scl);
    r.integer(i2c["muxAddress"], "i2c.muxAddress", cfg.i2c.muxAddress);
    r.integer(i2c["freqHz"], "i2c.freqHz", cfg.i2c.freqHz);

    r.boolean(root["simulateSensors"], "simulateSensors", cfg.simulateSensors);
    r.integer(root["fusion"]["staleS"], "fusion.staleS", cfg.staleS);
    r.quantityMap(root["fusion"]["tolerance"], "fusion.tolerance", cfg.tolerance);
    r.integer(root["overrideTimeoutS"], "overrideTimeoutS", cfg.overrideTimeoutS);

    if (r.array(root["sensors"], "sensors")) {
        cfg.sensors.clear();
        int i = 0;
        for (JsonVariantConst e : root["sensors"].as<JsonArrayConst>()) {
            std::string p = "sensors[" + std::to_string(i++) + "]";
            SensorConfig s;
            std::string type;
            r.string(e["id"], p + ".id", s.id);
            r.string(e["type"], p + ".type", type);
            if (!sensorTypeFromName(type, s.type)) r.fail(p + ".type", "must be scd41, bme680 or bme280");
            s.address = defaultAddress(s.type);
            r.string(e["group"], p + ".group", s.group);
            r.integer(e["muxChannel"], p + ".muxChannel", s.muxChannel);
            r.integer(e["address"], p + ".address", s.address);
            r.quantityMap(e["offset"], p + ".offset", s.offset);
            cfg.sensors.push_back(s);
        }
    }

    if (r.array(root["outputs"], "outputs")) {
        cfg.outputs.clear();
        int i = 0;
        for (JsonVariantConst e : root["outputs"].as<JsonArrayConst>()) {
            OutputConfig o;
            readOutput(r, e, "outputs[" + std::to_string(i++) + "]", o);
            cfg.outputs.push_back(o);
        }
    }

    if (r.array(root["alarms"], "alarms")) {
        cfg.alarms.clear();
        int i = 0;
        for (JsonVariantConst e : root["alarms"].as<JsonArrayConst>()) {
            std::string p = "alarms[" + std::to_string(i++) + "]";
            AlarmConfig a;
            r.string(e["signal"], p + ".signal", a.signal);
            r.number(e["min"], p + ".min", a.min);
            r.number(e["max"], p + ".max", a.max);
            r.integer(e["delayS"], p + ".delayS", a.delayS);
            cfg.alarms.push_back(a);
        }
    }

    return errors.size() == errorsBefore;
}

std::vector<std::string> validateConfig(const Config& cfg, Chip chip) {
    std::vector<std::string> errors;
    auto err = [&](const std::string& path, const std::string& msg) { errors.push_back(path + ": " + msg); };
    auto checkPin = [&](const std::string& path, int pin, PinUse use) {
        if (const char* problem = pinProblem(chip, pin, use)) err(path, "GPIO " + std::to_string(pin) + " " + problem);
    };

    if (!validName(cfg.deviceName)) err("device.name", "use 1-32 characters a-z, 0-9, '-' or '_'");

    if (cfg.wifi.apEnabled) {
        if (cfg.wifi.apSsid.empty() || cfg.wifi.apSsid.size() > 32) err("wifi.ap.ssid", "1 to 32 characters");
        if (cfg.wifi.apPassword.size() < 8 || cfg.wifi.apPassword.size() > 63)
            err("wifi.ap.password", "8 to 63 characters");
    } else if (cfg.wifi.staSsid.empty()) {
        err("wifi", "enable the access point or configure wifi.sta, otherwise the device is unreachable");
    }
    {
        std::set<std::string> userNames;
        bool admin = false;
        for (size_t i = 0; i < cfg.web.users.size(); i++) {
            const WebUser& u = cfg.web.users[i];
            std::string p = "web.users[" + std::to_string(i) + "]";
            if (!validName(u.name)) err(p + ".name", "use 1-32 characters a-z, 0-9, '-' or '_'");
            if (!userNames.insert(u.name).second) err(p + ".name", "duplicate user '" + u.name + "'");
            if (u.password.size() < 8) err(p + ".password", "at least 8 characters");
            admin |= u.role == Role::Admin;
        }
        if (!admin) err("web.users", "at least one user must have the role admin");
    }

    if (cfg.mqtt.enabled) {
        if (cfg.mqtt.host.empty()) err("mqtt.host", "required when MQTT is enabled");
        if (cfg.wifi.staSsid.empty()) err("mqtt", "needs wifi.sta to reach the broker");
        if (cfg.mqtt.port < 1 || cfg.mqtt.port > 65535) err("mqtt.port", "1 to 65535");
        if (cfg.mqtt.baseTopic.empty() || cfg.mqtt.baseTopic.find_first_of("+#") != std::string::npos)
            err("mqtt.baseTopic", "must be non-empty and contain no + or #");
    }
    if (cfg.mqtt.publishIntervalS < 1) err("mqtt.publishIntervalS", "at least 1");

    checkPin("i2c.sda", cfg.i2c.sda, PinUse::I2c);
    checkPin("i2c.scl", cfg.i2c.scl, PinUse::I2c);
    if (cfg.i2c.sda == cfg.i2c.scl) err("i2c", "sda and scl must differ");
    if (cfg.i2c.muxAddress < 0x70 || cfg.i2c.muxAddress > 0x77) err("i2c.muxAddress", "TCA9548 uses 0x70..0x77 (112..119)");
    if (cfg.i2c.freqHz < 10000 || cfg.i2c.freqHz > 400000) err("i2c.freqHz", "10000 to 400000");

    if (cfg.staleS < 5) err("fusion.staleS", "at least 5 seconds");
    for (int q = 0; q < kQuantityCount; q++) {
        if (!(cfg.tolerance[q] > 0)) err(std::string("fusion.tolerance.") + quantityName(Quantity(q)), "must be > 0");
    }

    std::set<std::string> ids;
    std::set<std::pair<int, int>> busSlots;
    for (size_t i = 0; i < cfg.sensors.size(); i++) {
        const SensorConfig& s = cfg.sensors[i];
        std::string p = "sensors[" + std::to_string(i) + "]";
        if (!validName(s.id)) err(p + ".id", "use 1-32 characters a-z, 0-9, '-' or '_'");
        if (!ids.insert(s.id).second) err(p + ".id", "duplicate id '" + s.id + "'");
        if (!validName(s.group)) err(p + ".group", "use 1-32 characters a-z, 0-9, '-' or '_'");
        if (s.muxChannel < -1 || s.muxChannel > 7) err(p + ".muxChannel", "0..7, or -1 for no multiplexer");
        if (s.address < 0x08 || s.address > 0x77) err(p + ".address", "7-bit address 0x08..0x77");
        if (!busSlots.insert({s.muxChannel, s.address}).second)
            err(p, "another sensor uses the same mux channel and address");
    }

    std::set<std::string> names;
    std::set<int> pins = {cfg.i2c.sda, cfg.i2c.scl};
    for (size_t i = 0; i < cfg.outputs.size(); i++) {
        const OutputConfig& o = cfg.outputs[i];
        std::string p = "outputs[" + std::to_string(i) + "]";
        if (!validName(o.name)) err(p + ".name", "use 1-32 characters a-z, 0-9, '-' or '_'");
        if (!names.insert(o.name).second) err(p + ".name", "duplicate name '" + o.name + "'");
        checkPin(p + ".pin", o.pin, PinUse::Output);
        if (!pins.insert(o.pin).second) err(p + ".pin", "GPIO " + std::to_string(o.pin) + " is already in use");
        if (o.kind == OutputKind::Pwm && (o.pwmFreqHz < 5 || o.pwmFreqHz > 40000)) err(p + ".pwmFreqHz", "5 to 40000");
        if (o.failsafe < 0 || o.failsafe > 1) err(p + ".failsafe", "0..1 (or true/false)");
        if (o.maxOnS > 0 && o.cooldownS == 0) err(p + ".cooldownS", "must be > 0 when maxOnS is set");

        for (size_t j = 0; j < o.rules.size(); j++) {
            const Rule& r = o.rules[j];
            std::string rp = p + ".rules[" + std::to_string(j) + "]";
            bool needsSignal = r.type == RuleType::Above || r.type == RuleType::Below || r.type == RuleType::Linear;
            if (needsSignal && !signalExists(cfg, r.signal))
                err(rp + ".signal", "'" + r.signal + "' is not provided by any sensor (format: group.quantity)");
            if (r.level <= 0 || r.level > 1) err(rp + ".level", "must be > 0 and <= 1");
            switch (r.type) {
                case RuleType::Above:
                    if (!(r.off < r.on)) err(rp, "'off' must be below 'on' for an above rule");
                    break;
                case RuleType::Below:
                    if (!(r.off > r.on)) err(rp, "'off' must be above 'on' for a below rule");
                    break;
                case RuleType::Schedule:
                    if (r.windows.empty()) err(rp + ".windows", "at least one window");
                    for (const TimeWindow& w : r.windows) {
                        if (w.fromMin >= 1440 || w.fromMin == w.toMin) err(rp + ".windows", "from must be before 24:00 and differ from to");
                    }
                    break;
                case RuleType::Cycle:
                    if (r.periodS == 0 || r.onS == 0 || r.onS > r.periodS) err(rp, "need 0 < onS <= periodS");
                    break;
                case RuleType::Linear:
                    if (!(r.inMax > r.inMin)) err(rp + ".in", "second value must be larger");
                    if (r.outMin < 0 || r.outMin > 1 || r.outMax < 0 || r.outMax > 1) err(rp + ".out", "levels 0..1");
                    break;
            }
        }
    }

    for (size_t i = 0; i < cfg.alarms.size(); i++) {
        const AlarmConfig& a = cfg.alarms[i];
        std::string p = "alarms[" + std::to_string(i) + "]";
        if (!signalExists(cfg, a.signal)) err(p + ".signal", "'" + a.signal + "' is not provided by any sensor");
        if (std::isnan(a.min) && std::isnan(a.max)) err(p, "set min, max or both");
        if (!std::isnan(a.min) && !std::isnan(a.max) && !(a.min < a.max)) err(p, "min must be below max");
    }
    return errors;
}

std::string configToJson(const Config& cfg, bool includeSecrets) {
    JsonDocument doc;
    auto secret = [&](const std::string& s) { return includeSecrets || s.empty() ? s : std::string(kSecretMask); };

    doc["device"]["name"] = cfg.deviceName;
    doc["wifi"]["ap"]["enabled"] = cfg.wifi.apEnabled;
    doc["wifi"]["ap"]["ssid"] = cfg.wifi.apSsid;
    doc["wifi"]["ap"]["password"] = secret(cfg.wifi.apPassword);
    doc["wifi"]["sta"]["ssid"] = cfg.wifi.staSsid;
    doc["wifi"]["sta"]["password"] = secret(cfg.wifi.staPassword);
    JsonArray users = doc["web"]["users"].to<JsonArray>();
    for (const WebUser& u : cfg.web.users) {
        JsonObject o = users.add<JsonObject>();
        o["name"] = u.name;
        o["password"] = secret(u.password);
        o["role"] = roleName(u.role);
    }

    JsonObject mqtt = doc["mqtt"].to<JsonObject>();
    mqtt["enabled"] = cfg.mqtt.enabled;
    mqtt["host"] = cfg.mqtt.host;
    mqtt["port"] = cfg.mqtt.port;
    mqtt["user"] = cfg.mqtt.user;
    mqtt["password"] = secret(cfg.mqtt.password);
    mqtt["baseTopic"] = cfg.mqtt.baseTopic;
    mqtt["publishIntervalS"] = cfg.mqtt.publishIntervalS;

    doc["time"]["tz"] = cfg.time.tz;
    doc["time"]["ntp"] = cfg.time.ntpServer;

    doc["i2c"]["sda"] = cfg.i2c.sda;
    doc["i2c"]["scl"] = cfg.i2c.scl;
    doc["i2c"]["muxAddress"] = cfg.i2c.muxAddress;
    doc["i2c"]["freqHz"] = cfg.i2c.freqHz;

    doc["simulateSensors"] = cfg.simulateSensors;
    doc["fusion"]["staleS"] = cfg.staleS;
    JsonObject tol = doc["fusion"]["tolerance"].to<JsonObject>();
    for (int q = 0; q < kQuantityCount; q++) tol[quantityName(Quantity(q))] = cfg.tolerance[q];
    doc["overrideTimeoutS"] = cfg.overrideTimeoutS;

    JsonArray sensors = doc["sensors"].to<JsonArray>();
    for (const SensorConfig& s : cfg.sensors) {
        JsonObject o = sensors.add<JsonObject>();
        o["id"] = s.id;
        o["type"] = sensorTypeName(s.type);
        o["group"] = s.group;
        o["muxChannel"] = s.muxChannel;
        o["address"] = s.address;
        for (int q = 0; q < kQuantityCount; q++) {
            if (s.offset[q] != 0) o["offset"][quantityName(Quantity(q))] = s.offset[q];
        }
    }

    JsonArray outputs = doc["outputs"].to<JsonArray>();
    for (const OutputConfig& oc : cfg.outputs) {
        JsonObject o = outputs.add<JsonObject>();
        o["name"] = oc.name;
        o["label"] = oc.label;
        o["pin"] = oc.pin;
        o["kind"] = oc.kind == OutputKind::Pwm ? "pwm" : "relay";
        o["activeHigh"] = oc.activeHigh;
        if (oc.kind == OutputKind::Pwm) o["pwmFreqHz"] = oc.pwmFreqHz;
        o["failsafe"] = oc.failsafe;
        o["minOnS"] = oc.minOnS;
        o["minOffS"] = oc.minOffS;
        o["maxOnS"] = oc.maxOnS;
        o["cooldownS"] = oc.cooldownS;
        JsonArray rules = o["rules"].to<JsonArray>();
        for (const Rule& r : oc.rules) {
            JsonObject ro = rules.add<JsonObject>();
            ro["type"] = ruleTypeName(r.type);
            switch (r.type) {
                case RuleType::Above:
                case RuleType::Below:
                    ro["signal"] = r.signal;
                    ro["on"] = r.on;
                    ro["off"] = r.off;
                    break;
                case RuleType::Schedule: {
                    JsonArray ws = ro["windows"].to<JsonArray>();
                    for (const TimeWindow& w : r.windows) {
                        JsonObject wo = ws.add<JsonObject>();
                        wo["from"] = formatTime(w.fromMin);
                        wo["to"] = formatTime(w.toMin);
                    }
                    break;
                }
                case RuleType::Cycle:
                    ro["onS"] = r.onS;
                    ro["periodS"] = r.periodS;
                    break;
                case RuleType::Linear: {
                    ro["signal"] = r.signal;
                    JsonArray in = ro["in"].to<JsonArray>();
                    in.add(r.inMin);
                    in.add(r.inMax);
                    JsonArray out = ro["out"].to<JsonArray>();
                    out.add(r.outMin);
                    out.add(r.outMax);
                    break;
                }
            }
            if (r.type != RuleType::Linear) ro["level"] = r.level;
        }
    }

    JsonArray alarms = doc["alarms"].to<JsonArray>();
    for (const AlarmConfig& a : cfg.alarms) {
        JsonObject o = alarms.add<JsonObject>();
        o["signal"] = a.signal;
        if (!std::isnan(a.min)) o["min"] = a.min;
        if (!std::isnan(a.max)) o["max"] = a.max;
        o["delayS"] = a.delayS;
    }

    std::string out;
    serializeJsonPretty(doc, out);
    return out;
}

Config defaultConfig(Chip chip) {
    Config c;
    const bool s3 = chip == Chip::Esp32S3;
    c.i2c.sda = s3 ? 8 : 21;
    c.i2c.scl = s3 ? 9 : 22;
    // Placeholder pins until the board's schematic is known: none of them are strapping pins.
    const int esp32Pins[8] = {16, 17, 18, 19, 23, 25, 26, 27};
    const int s3Pins[8] = {4, 5, 6, 7, 15, 16, 17, 18};
    const int* pin = s3 ? s3Pins : esp32Pins;

    auto sensor = [&](const char* id, SensorType type, const char* group, int channel) {
        SensorConfig s;
        s.id = id;
        s.type = type;
        s.group = group;
        s.muxChannel = channel;
        s.address = defaultAddress(type);
        c.sensors.push_back(s);
    };
    sensor("chamber_scd41_a", SensorType::Scd41, "chamber", 0);
    sensor("chamber_bme680_a", SensorType::Bme680, "chamber", 0);
    sensor("chamber_scd41_b", SensorType::Scd41, "chamber", 1);
    sensor("chamber_bme680_b", SensorType::Bme680, "chamber", 1);
    sensor("inflow", SensorType::Bme280, "inflow", 2);
    sensor("ambient", SensorType::Bme280, "ambient", 3);

    auto above = [](const char* signal, float on, float off) {
        Rule r;
        r.type = RuleType::Above;
        r.signal = signal;
        r.on = on;
        r.off = off;
        return r;
    };
    auto cycle = [](uint32_t onS, uint32_t periodS) {
        Rule r;
        r.type = RuleType::Cycle;
        r.onS = onS;
        r.periodS = periodS;
        return r;
    };
    auto output = [&](const char* name, const char* label, int n, float failsafe, std::vector<Rule> rules) {
        OutputConfig o;
        o.name = name;
        o.label = label;
        o.pin = pin[n];
        o.failsafe = failsafe;
        o.rules = std::move(rules);
        c.outputs.push_back(o);
        return &c.outputs.back();
    };

    // Fans default to ON when data is missing: fresh air is the safest state for the mushrooms.
    output("hepa", "Inflow HEPA", 0, 1, {above("chamber.co2", 1000, 800)})->minOnS = 60;

    Rule humid;
    humid.type = RuleType::Below;
    humid.signal = "chamber.humidity";
    humid.on = 85;
    humid.off = 90;
    OutputConfig* humidifier = output("humidifier", "Breeding humidifier", 1, 0, {humid});
    humidifier->minOnS = 30;
    humidifier->minOffS = 30;
    humidifier->maxOnS = 1800;

    output("uv", "Breeding UV (manual only)", 2, 0, {});

    Rule light;
    light.type = RuleType::Schedule;
    light.windows.push_back({8 * 60, 20 * 60});
    output("light", "Breeding light", 3, 0, {light});

    output("inlet", "Breeding inlet air", 4, 1, {above("chamber.co2", 1000, 800), cycle(300, 1800)})->minOnS = 60;
    output("outlet", "Breeding outlet air", 5, 1,
           {above("chamber.co2", 1000, 800), above("chamber.humidity", 97, 94), cycle(300, 1800)})
        ->minOnS = 60;

    Rule fanCurve;
    fanCurve.type = RuleType::Linear;
    fanCurve.signal = "chamber.co2";
    fanCurve.inMin = 800;
    fanCurve.inMax = 1500;
    fanCurve.outMin = 0.3f;
    fanCurve.outMax = 1.0f;
    output("fan_pwm_1", "Fan speed 1", 6, 1, {fanCurve})->kind = OutputKind::Pwm;
    output("fan_pwm_2", "Fan speed 2 (unused)", 7, 0, {})->kind = OutputKind::Pwm;

    AlarmConfig a;
    a.signal = "chamber.humidity";
    a.min = 75;
    c.alarms.push_back(a);
    a = AlarmConfig();
    a.signal = "chamber.co2";
    a.max = 2000;
    c.alarms.push_back(a);
    a = AlarmConfig();
    a.signal = "chamber.temperature";
    a.min = 12;
    a.max = 30;
    c.alarms.push_back(a);
    return c;
}

}  // namespace cc
