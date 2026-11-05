#include "Outputs.h"

#include <Arduino.h>

static const int kPwmBits = 10;
static const int kPwmMax = (1 << kPwmBits) - 1;

bool Outputs::sameHardware(const Channel& a, const Channel& b) {
    return a.pin == b.pin && a.pwm == b.pwm && a.activeHigh == b.activeHigh && a.freqHz == b.freqHz;
}

void Outputs::configure(const cc::Config& cfg) {
    std::vector<Channel> next;
    int pwmIndex = 0;
    for (const cc::OutputConfig& oc : cfg.outputs) {
        Channel c;
        c.pin = oc.pin;
        c.pwm = oc.kind == cc::OutputKind::Pwm;
        c.activeHigh = oc.activeHigh;
        c.freqHz = c.pwm ? oc.pwmFreqHz : 0;
        // Arduino-ESP32 2.x: LEDC channels 2n and 2n+1 share a timer, use even ones so every
        // PWM output can have its own frequency.
        if (c.pwm) c.ledc = 2 * pwmIndex++;
        next.push_back(c);
    }

    // Release pins that are no longer used, or used differently.
    for (const Channel& old : channels_) {
        bool kept = false;
        for (const Channel& c : next) kept |= sameHardware(old, c) && old.ledc == c.ledc;
        if (!kept) release(old);
    }
    for (size_t i = 0; i < next.size(); i++) {
        bool existed = false;
        for (const Channel& old : channels_) existed |= sameHardware(old, next[i]) && old.ledc == next[i].ledc;
        if (!existed) setup(next[i], cfg.outputs[i].failsafe);
    }
    channels_ = next;
}

void Outputs::apply(const std::vector<cc::OutputState>& states) {
    for (size_t i = 0; i < channels_.size() && i < states.size(); i++) write(channels_[i], states[i].level);
}

void Outputs::setup(const Channel& c, float level) {
    if (c.pwm) {
#if ESP_ARDUINO_VERSION_MAJOR >= 3
        ledcAttach(c.pin, c.freqHz, kPwmBits);
#else
        ledcSetup(c.ledc, c.freqHz, kPwmBits);
        ledcAttachPin(c.pin, c.ledc);
#endif
    } else {
        // Set the level before enabling the driver, so the pin never shows the wrong state.
        digitalWrite(c.pin, (level > 0) == c.activeHigh ? HIGH : LOW);
        pinMode(c.pin, OUTPUT);
    }
    write(c, level);
}

void Outputs::write(const Channel& c, float level) {
    if (c.pwm) {
        int duty = static_cast<int>(level * kPwmMax + 0.5f);
        if (!c.activeHigh) duty = kPwmMax - duty;
#if ESP_ARDUINO_VERSION_MAJOR >= 3
        ledcWrite(c.pin, duty);
#else
        ledcWrite(c.ledc, duty);
#endif
    } else {
        digitalWrite(c.pin, (level > 0) == c.activeHigh ? HIGH : LOW);
    }
}

void Outputs::release(const Channel& c) {
    write(c, 0);
    if (c.pwm) {
#if ESP_ARDUINO_VERSION_MAJOR >= 3
        ledcDetach(c.pin);
#else
        ledcDetachPin(c.pin);
#endif
    }
    // Weak pull towards "off" instead of a floating input.
    pinMode(c.pin, c.activeHigh ? INPUT_PULLDOWN : INPUT_PULLUP);
}
