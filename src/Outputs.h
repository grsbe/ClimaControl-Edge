#pragma once
// Drives the SSR outputs: plain on/off GPIOs for relays, LEDC hardware PWM for fan speed outputs.

#include <vector>

#include "Config.h"
#include "Controller.h"

class Outputs {
   public:
    // Puts every output into its failsafe level. Outputs whose pin and type did not change keep
    // their current level, so reconfiguring does not make the relays click.
    void configure(const cc::Config& cfg);
    // states[i] belongs to cfg.outputs[i] of the last configure()
    void apply(const std::vector<cc::OutputState>& states);

   private:
    struct Channel {
        int pin = -1;
        bool pwm = false;
        bool activeHigh = true;
        uint32_t freqHz = 0;
        int ledc = -1;
    };
    static bool sameHardware(const Channel& a, const Channel& b);
    void setup(const Channel& c, float level);
    void write(const Channel& c, float level);
    void release(const Channel& c);

    std::vector<Channel> channels_;
};
