#include "Types.h"

namespace cc {

static const char* const kQuantityNames[kQuantityCount] = {"temperature", "humidity", "co2", "pressure",
                                                            "gas"};

const char* quantityName(Quantity q) { return kQuantityNames[idx(q)]; }

bool quantityFromName(const std::string& name, Quantity& out) {
    for (int i = 0; i < kQuantityCount; i++) {
        if (name == kQuantityNames[i]) {
            out = static_cast<Quantity>(i);
            return true;
        }
    }
    return false;
}

Range plausibleRange(Quantity q) {
    switch (q) {
        case Quantity::Temperature: return {-40.0f, 85.0f};
        case Quantity::Humidity: return {0.0f, 100.0f};
        case Quantity::Co2: return {0.0f, 40000.0f};
        case Quantity::Pressure: return {300.0f, 1100.0f};
        case Quantity::Gas: return {0.0f, 100000.0f};
        default: return {0.0f, 0.0f};
    }
}

const char* sensorTypeName(SensorType t) {
    switch (t) {
        case SensorType::Scd41: return "scd41";
        case SensorType::Bme680: return "bme680";
        case SensorType::Bme280: return "bme280";
    }
    return "?";
}

bool sensorTypeFromName(const std::string& name, SensorType& out) {
    for (SensorType t : {SensorType::Scd41, SensorType::Bme680, SensorType::Bme280}) {
        if (name == sensorTypeName(t)) {
            out = t;
            return true;
        }
    }
    return false;
}

bool sensorProvides(SensorType t, Quantity q) {
    switch (t) {
        case SensorType::Scd41:
            return q == Quantity::Temperature || q == Quantity::Humidity || q == Quantity::Co2;
        case SensorType::Bme680:
            return q == Quantity::Temperature || q == Quantity::Humidity || q == Quantity::Pressure ||
                   q == Quantity::Gas;
        case SensorType::Bme280:
            return q == Quantity::Temperature || q == Quantity::Humidity || q == Quantity::Pressure;
    }
    return false;
}

}  // namespace cc
