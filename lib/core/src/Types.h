#pragma once
// Basic types shared by all core modules. Nothing in lib/core depends on Arduino,
// so the whole control logic can be unit-tested on a PC.

#include <cstdint>
#include <string>

namespace cc {

// Units: temperature degC, humidity %RH, co2 ppm, pressure hPa, gas kOhm (BME680 gas resistance)
enum class Quantity : uint8_t { Temperature, Humidity, Co2, Pressure, Gas, Count };
constexpr int kQuantityCount = static_cast<int>(Quantity::Count);

inline int idx(Quantity q) { return static_cast<int>(q); }
const char* quantityName(Quantity q);
bool quantityFromName(const std::string& name, Quantity& out);

// Values outside this range are physically impossible for the sensor and count as a fault.
struct Range {
    float min;
    float max;
};
Range plausibleRange(Quantity q);

enum class SensorType : uint8_t { Scd41, Bme680, Bme280 };
const char* sensorTypeName(SensorType t);
bool sensorTypeFromName(const std::string& name, SensorType& out);
bool sensorProvides(SensorType t, Quantity q);

struct Measurement {
    bool has[kQuantityCount] = {};
    float value[kQuantityCount] = {};

    void set(Quantity q, float v) {
        has[idx(q)] = true;
        value[idx(q)] = v;
    }
};

// Latest reading of one physical sensor.
struct SensorSample {
    bool ok = false;        // last read attempt succeeded
    uint32_t atMs = 0;      // when `m` was measured
    uint16_t failures = 0;  // consecutive failed reads
    Measurement m;
};

// Wrap-safe millisecond comparison: true if a is earlier than b.
inline bool before(uint32_t a, uint32_t b) { return static_cast<int32_t>(a - b) < 0; }

}  // namespace cc
