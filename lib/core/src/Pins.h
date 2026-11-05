#pragma once
// Which GPIOs may be used for what. Some ESP32 pins are wired to the flash chip, are input-only,
// or are "strapping" pins that the boot ROM reads and that can toggle during reset. An SSR on a
// strapping pin could briefly switch a 230 V load on at every reboot, so outputs refuse them.

namespace cc {

enum class Chip { Esp32, Esp32S3 };
enum class PinUse { Output, Input, I2c };

// nullptr if the pin is fine for this use, otherwise a human-readable reason.
const char* pinProblem(Chip chip, int pin, PinUse use);

}  // namespace cc
