#pragma once
// A copy of everything the control loop knows, handed to the web UI and MQTT.

#include <ArduinoJson.h>

#include <string>
#include <vector>

#include "Controller.h"
#include "Fusion.h"

namespace cc {

struct SensorStatus {
    std::string id;
    SensorType type = SensorType::Bme280;
    std::string group;
    SensorSample sample;
};

struct OutputStatus {
    std::string name;
    std::string label;
    OutputKind kind = OutputKind::Relay;
    OutputState state;
    uint32_t manualRemainingS = 0;
};

struct Snapshot {
    uint32_t uptimeS = 0;
    uint32_t nowMs = 0;
    int64_t epoch = 0;  // 0 if the clock is not set
    bool simulated = false;
    std::vector<Signal> signals;
    std::vector<SensorStatus> sensors;
    std::vector<OutputStatus> outputs;
    std::vector<Alarm> alarms;
};

Snapshot makeSnapshot(const Config& cfg, const std::vector<SensorSample>& samples, const Fusion& fusion,
                      const Controller& controller, uint32_t nowMs, int64_t epoch);

void snapshotToJson(const Snapshot& s, JsonObject out);
void alarmsToJson(const std::vector<Alarm>& alarms, JsonArray out);

}  // namespace cc
