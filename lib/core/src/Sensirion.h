#pragma once
// Protocol helpers for the Sensirion SCD41 (CO2 sensor). Kept free of I2C calls so they can be tested.

#include <cstddef>
#include <cstdint>

namespace cc {
namespace scd41 {

constexpr uint16_t kStartPeriodicMeasurement = 0x21B1;
constexpr uint16_t kStopPeriodicMeasurement = 0x3F86;
constexpr uint16_t kReadMeasurement = 0xEC05;
constexpr uint16_t kGetDataReadyStatus = 0xE4B8;

// CRC-8 over each 16-bit word: polynomial 0x31, init 0xFF.
uint8_t crc8(const uint8_t* data, size_t len);

// Checks the CRC of `words` 3-byte groups (2 data bytes + CRC) and extracts the data words.
bool decodeWords(const uint8_t* raw, size_t words, uint16_t* out);

// Converts a read_measurement response (9 bytes). Returns false on CRC error.
bool decodeMeasurement(const uint8_t raw[9], float& co2, float& temperature, float& humidity);

inline bool dataReady(uint16_t status) { return (status & 0x07FF) != 0; }

}  // namespace scd41
}  // namespace cc
