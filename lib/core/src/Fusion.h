#pragma once
// Combines the readings of redundant sensors into one value per "signal" (group + quantity),
// e.g. chamber.humidity from 2x SCD41 + 2x BME680. Faulty, stale and implausible readings are
// dropped; the median of the rest is used. If no reading is left the signal is invalid, and
// every rule using it falls back to its output's failsafe level.

#include <string>
#include <vector>

#include "Config.h"

namespace cc {

struct Signal {
    std::string name;  // "chamber.co2"
    Quantity quantity = Quantity::Temperature;
    float value = NAN;
    bool valid = false;
    int sources = 0;   // readings that went into the value
    int expected = 0;  // sensors configured for this signal
    bool disagree = false;  // spread between sensors exceeds the configured tolerance
    std::vector<int> sensorIndexes;
};

// Applies the per-sensor calibration offsets in place.
void applyCalibration(const Config& cfg, std::vector<SensorSample>& samples);

class Fusion {
   public:
    void configure(const Config& cfg);
    // samples[i] belongs to cfg.sensors[i]
    void update(const std::vector<SensorSample>& samples, uint32_t nowMs);

    const std::vector<Signal>& signals() const { return signals_; }
    const Signal* find(const std::string& name) const;

   private:
    std::vector<Signal> signals_;
    uint32_t staleMs_ = 30000;
    float tolerance_[kQuantityCount] = {};
};

}  // namespace cc
