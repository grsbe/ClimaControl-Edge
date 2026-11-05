#include "Pins.h"

namespace cc {

static const char* esp32Problem(int pin, PinUse use) {
    if (pin < 0 || pin > 39 || pin == 20 || pin == 24 || (pin >= 28 && pin <= 31)) return "no such GPIO on ESP32";
    if (pin >= 6 && pin <= 11) return "connected to the flash chip";
    if (pin == 1 || pin == 3) return "used by the USB serial console";
    if (pin >= 34 && use != PinUse::Input) return "input-only pin";
    if (use == PinUse::Output && (pin == 0 || pin == 2 || pin == 5 || pin == 12 || pin == 15))
        return "strapping pin, may toggle during boot";
    if (use == PinUse::I2c && pin == 12) return "strapping pin (flash voltage), pull-ups can break boot";
    return nullptr;
}

static const char* esp32s3Problem(int pin, PinUse use) {
    if (pin < 0 || pin > 48 || (pin >= 22 && pin <= 25)) return "no such GPIO on ESP32-S3";
    if (pin >= 26 && pin <= 32) return "connected to the flash/PSRAM chip";
    if (pin >= 33 && pin <= 37) return "used by octal PSRAM on R8 modules";
    if (pin == 19 || pin == 20) return "native USB pins";
    if (pin == 43 || pin == 44) return "used by the USB serial console";
    if (use == PinUse::Output && (pin == 0 || pin == 3 || pin == 45 || pin == 46))
        return "strapping pin, may toggle during boot";
    return nullptr;
}

const char* pinProblem(Chip chip, int pin, PinUse use) {
    return chip == Chip::Esp32 ? esp32Problem(pin, use) : esp32s3Problem(pin, use);
}

}  // namespace cc
