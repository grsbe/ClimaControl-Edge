#include "Simulation.h"

#include <algorithm>

namespace cc {

static const float kAmbientTemperature = 16.0f;
static const float kAmbientHumidity = 60.0f;
static const float kAmbientCo2 = 420.0f;

void Simulation::step(float dtS, const std::vector<OutputConfig>& configs, const std::vector<OutputState>& outputs) {
    float humidify = 0, ventilate = 0.02f, heat = 0;  // a little natural air exchange
    for (size_t i = 0; i < configs.size() && i < outputs.size(); i++) {
        const std::string& n = configs[i].name;
        const float level = outputs[i].level;
        if (n.find("humid") != std::string::npos) humidify += level;
        if (n.find("inlet") != std::string::npos || n.find("outlet") != std::string::npos ||
            n.find("hepa") != std::string::npos || n.find("fan") != std::string::npos)
            ventilate += 0.25f * level;
        if (n.find("light") != std::string::npos) heat += level;
    }
    // per-second rates
    const float exchange = std::min(1.0f, ventilate * 0.004f * dtS);
    temperature += (kAmbientTemperature - temperature) * exchange + (0.004f + heat * 0.003f) * dtS;
    humidity += (kAmbientHumidity - humidity) * exchange + humidify * 0.3f * dtS;
    co2 += (kAmbientCo2 - co2) * exchange + 1.0f * dtS;
    humidity = std::min(100.0f, std::max(0.0f, humidity));
}

float Simulation::noise() {
    seed_ = seed_ * 1664525u + 1013904223u;
    return static_cast<float>(seed_ >> 8) / static_cast<float>(1u << 24) - 0.5f;  // -0.5 .. 0.5
}

void Simulation::fill(const Config& cfg, uint32_t nowMs, std::vector<SensorSample>& samples) {
    samples.resize(cfg.sensors.size());
    for (size_t i = 0; i < cfg.sensors.size(); i++) {
        const SensorConfig& sc = cfg.sensors[i];
        const bool chamber = sc.group == "chamber";
        SensorSample& s = samples[i];
        s = SensorSample();
        s.ok = true;
        s.atMs = nowMs;
        const float t = chamber ? temperature : kAmbientTemperature;
        const float h = chamber ? humidity : kAmbientHumidity;
        const float c = chamber ? co2 : kAmbientCo2;
        if (sensorProvides(sc.type, Quantity::Temperature)) s.m.set(Quantity::Temperature, t + 0.4f * noise());
        if (sensorProvides(sc.type, Quantity::Humidity)) s.m.set(Quantity::Humidity, h + 1.0f * noise());
        if (sensorProvides(sc.type, Quantity::Co2)) s.m.set(Quantity::Co2, c + 30.0f * noise());
        if (sensorProvides(sc.type, Quantity::Pressure)) s.m.set(Quantity::Pressure, 1013.0f + noise());
        if (sensorProvides(sc.type, Quantity::Gas)) s.m.set(Quantity::Gas, 120.0f + 10.0f * noise());
    }
}

}  // namespace cc
