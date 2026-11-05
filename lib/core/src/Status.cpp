#include "Status.h"

#include <cmath>

namespace cc {

// One decimal is plenty and keeps the JSON small.
static float round1(float v) { return std::round(v * 10.0f) / 10.0f; }

Snapshot makeSnapshot(const Config& cfg, const std::vector<SensorSample>& samples, const Fusion& fusion,
                      const Controller& controller, uint32_t nowMs, int64_t epoch) {
    Snapshot s;
    s.nowMs = nowMs;
    s.uptimeS = nowMs / 1000;
    s.epoch = epoch;
    s.simulated = cfg.simulateSensors;
    s.signals = fusion.signals();
    for (size_t i = 0; i < cfg.sensors.size(); i++) {
        SensorStatus ss;
        ss.id = cfg.sensors[i].id;
        ss.type = cfg.sensors[i].type;
        ss.group = cfg.sensors[i].group;
        if (i < samples.size()) ss.sample = samples[i];
        s.sensors.push_back(ss);
    }
    const auto& states = controller.outputs();
    const auto& configs = controller.outputConfigs();
    for (size_t i = 0; i < states.size(); i++) {
        OutputStatus os;
        os.name = configs[i].name;
        os.label = configs[i].label;
        os.kind = configs[i].kind;
        os.state = states[i];
        os.manualRemainingS = controller.manualRemainingS(states[i], nowMs);
        s.outputs.push_back(os);
    }
    s.alarms = controller.alarms();
    return s;
}

void alarmsToJson(const std::vector<Alarm>& alarms, JsonArray out) {
    for (const Alarm& a : alarms) {
        JsonObject o = out.add<JsonObject>();
        o["code"] = a.code;
        o["message"] = a.message;
        o["critical"] = a.critical;
    }
}

void snapshotToJson(const Snapshot& s, JsonObject out) {
    out["uptimeS"] = s.uptimeS;
    if (s.epoch > 0) out["epoch"] = s.epoch;
    else out["epoch"] = nullptr;
    out["simulated"] = s.simulated;

    JsonObject signals = out["signals"].to<JsonObject>();
    for (const Signal& sig : s.signals) {
        JsonObject o = signals[sig.name].to<JsonObject>();
        if (sig.valid) o["value"] = round1(sig.value);
        else o["value"] = nullptr;
        o["sources"] = sig.sources;
        o["expected"] = sig.expected;
        if (sig.disagree) o["disagree"] = true;
    }

    JsonArray sensors = out["sensors"].to<JsonArray>();
    for (const SensorStatus& ss : s.sensors) {
        JsonObject o = sensors.add<JsonObject>();
        o["id"] = ss.id;
        o["type"] = sensorTypeName(ss.type);
        o["group"] = ss.group;
        o["ok"] = ss.sample.ok;
        o["failures"] = ss.sample.failures;
        if (ss.sample.atMs > 0) o["ageS"] = (s.nowMs - ss.sample.atMs) / 1000;
        JsonObject values = o["values"].to<JsonObject>();
        for (int q = 0; q < kQuantityCount; q++) {
            if (ss.sample.m.has[q]) values[quantityName(Quantity(q))] = round1(ss.sample.m.value[q]);
        }
    }

    JsonArray outputs = out["outputs"].to<JsonArray>();
    for (const OutputStatus& os : s.outputs) {
        JsonObject o = outputs.add<JsonObject>();
        o["name"] = os.name;
        o["label"] = os.label;
        o["kind"] = os.kind == OutputKind::Pwm ? "pwm" : "relay";
        o["on"] = os.state.on;
        o["level"] = std::round(os.state.level * 100.0f) / 100.0f;
        o["mode"] = os.state.mode == OutputMode::Manual ? "manual" : "auto";
        o["reason"] = os.state.reason;
        if (os.manualRemainingS > 0) o["manualRemainingS"] = os.manualRemainingS;
    }

    alarmsToJson(s.alarms, out["alarms"].to<JsonArray>());
}

}  // namespace cc
