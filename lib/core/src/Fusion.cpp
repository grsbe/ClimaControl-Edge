#include "Fusion.h"

#include <algorithm>

namespace cc {

void applyCalibration(const Config& cfg, std::vector<SensorSample>& samples) {
    for (size_t i = 0; i < samples.size() && i < cfg.sensors.size(); i++) {
        for (int q = 0; q < kQuantityCount; q++) {
            if (samples[i].m.has[q]) samples[i].m.value[q] += cfg.sensors[i].offset[q];
        }
    }
}

void Fusion::configure(const Config& cfg) {
    signals_.clear();
    staleMs_ = cfg.staleS * 1000;
    std::copy(std::begin(cfg.tolerance), std::end(cfg.tolerance), tolerance_);

    for (size_t i = 0; i < cfg.sensors.size(); i++) {
        const SensorConfig& s = cfg.sensors[i];
        for (int q = 0; q < kQuantityCount; q++) {
            if (!sensorProvides(s.type, Quantity(q))) continue;
            std::string name = s.group + "." + quantityName(Quantity(q));
            auto it = std::find_if(signals_.begin(), signals_.end(), [&](const Signal& sig) { return sig.name == name; });
            if (it == signals_.end()) {
                Signal sig;
                sig.name = name;
                sig.quantity = Quantity(q);
                signals_.push_back(sig);
                it = signals_.end() - 1;
            }
            it->sensorIndexes.push_back(static_cast<int>(i));
            it->expected++;
        }
    }
}

void Fusion::update(const std::vector<SensorSample>& samples, uint32_t nowMs) {
    std::vector<float> values;
    for (Signal& sig : signals_) {
        const int q = idx(sig.quantity);
        const Range range = plausibleRange(sig.quantity);
        values.clear();
        for (int i : sig.sensorIndexes) {
            if (i >= static_cast<int>(samples.size())) continue;
            const SensorSample& s = samples[i];
            if (!s.m.has[q] || nowMs - s.atMs > staleMs_) continue;
            const float v = s.m.value[q];
            if (!(v >= range.min && v <= range.max)) continue;  // also rejects NaN
            values.push_back(v);
        }

        sig.sources = static_cast<int>(values.size());
        sig.valid = !values.empty();
        sig.disagree = false;
        if (!sig.valid) {
            sig.value = NAN;
            continue;
        }
        std::sort(values.begin(), values.end());
        const size_t n = values.size();
        sig.value = n % 2 ? values[n / 2] : (values[n / 2 - 1] + values[n / 2]) / 2;
        sig.disagree = values.back() - values.front() > tolerance_[q];
    }
}

const Signal* Fusion::find(const std::string& name) const {
    for (const Signal& s : signals_) {
        if (s.name == name) return &s;
    }
    return nullptr;
}

}  // namespace cc
