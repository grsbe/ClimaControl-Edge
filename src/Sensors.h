#pragma once
// Reads all configured sensors through the TCA9548 I2C multiplexer, or simulates them.
// A sensor that stops answering is re-initialised every few seconds; if the multiplexer itself
// stops answering, the I2C bus is reset (a slave holding SDA low is a classic way to hang a bus).

#include <memory>
#include <vector>

#include "Config.h"
#include "Controller.h"
#include "Simulation.h"

class Adafruit_BME280;
class Adafruit_BME680;

class Sensors {
   public:
    Sensors();
    ~Sensors();
    void configure(const cc::Config& cfg);
    // Reads whatever is due and returns the latest raw sample for each configured sensor.
    const std::vector<cc::SensorSample>& poll(uint32_t nowMs, const cc::Controller& controller);
    bool simulated() const { return simulate_; }

   private:
    enum class Result { Fresh, NotReady, Error };
    struct Device;

    bool select(int channel);
    bool init(Device& d);
    Result read(Device& d, cc::SensorSample& s);
    Result readScd41(Device& d, cc::SensorSample& s);
    void recoverBus();

    cc::Config cfg_;
    bool simulate_ = false;
    cc::Simulation sim_;
    uint32_t lastSimMs_ = 0;
    std::vector<std::unique_ptr<Device>> devices_;
    std::vector<cc::SensorSample> samples_;
    int busFailures_ = 0;
};
