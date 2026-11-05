#pragma once
// A crude model of the grow room so the firmware can run without any sensors attached.
// Outputs are recognised by name: humidifier raises humidity, inlet/outlet/hepa/fan* ventilate
// (pull temperature, humidity and CO2 towards the ambient air), mushrooms keep producing CO2.

#include <cstdint>
#include <vector>

#include "Controller.h"

namespace cc {

class Simulation {
   public:
    void step(float dtS, const std::vector<OutputConfig>& configs, const std::vector<OutputState>& outputs);
    // One sample per configured sensor, with a little per-sensor noise.
    void fill(const Config& cfg, uint32_t nowMs, std::vector<SensorSample>& samples);

    float temperature = 20.0f;
    float humidity = 80.0f;
    float co2 = 900.0f;

   private:
    float noise();
    uint32_t seed_ = 12345;
};

}  // namespace cc
