#include "Sensirion.h"

namespace cc {
namespace scd41 {

uint8_t crc8(const uint8_t* data, size_t len) {
    uint8_t crc = 0xFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; bit++) crc = (crc & 0x80) ? static_cast<uint8_t>((crc << 1) ^ 0x31) : crc << 1;
    }
    return crc;
}

bool decodeWords(const uint8_t* raw, size_t words, uint16_t* out) {
    for (size_t i = 0; i < words; i++) {
        const uint8_t* w = raw + 3 * i;
        if (crc8(w, 2) != w[2]) return false;
        out[i] = static_cast<uint16_t>(w[0] << 8 | w[1]);
    }
    return true;
}

bool decodeMeasurement(const uint8_t raw[9], float& co2, float& temperature, float& humidity) {
    uint16_t w[3];
    if (!decodeWords(raw, 3, w)) return false;
    co2 = w[0];
    temperature = -45.0f + 175.0f * w[1] / 65535.0f;
    humidity = 100.0f * w[2] / 65535.0f;
    return true;
}

}  // namespace scd41
}  // namespace cc
