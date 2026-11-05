#include "Sensors.h"

#include <Adafruit_BME280.h>
#include <Adafruit_BME680.h>
#include <Arduino.h>
#include <Wire.h>

#include "Sensirion.h"

static const char* TAG = "sensors";
static const uint32_t kRetryInitMs = 5000;
static const uint8_t kBoschChipIdRegister = 0xD0;
static const uint8_t kBme280ChipId = 0x60;
static const uint8_t kBme680ChipId = 0x61;

struct Sensors::Device {
    cc::SensorConfig cfg;
    bool ready = false;
    uint32_t nextInitMs = 0;
    uint32_t nextReadMs = 0;
    uint32_t intervalMs = 2000;
    std::unique_ptr<Adafruit_BME280> bme280;
    std::unique_ptr<Adafruit_BME680> bme680;
};

Sensors::Sensors() = default;
Sensors::~Sensors() = default;

// ---------- low-level I2C ----------

static bool i2cCommand(uint8_t addr, uint16_t cmd) {
    Wire.beginTransmission(addr);
    Wire.write(static_cast<uint8_t>(cmd >> 8));
    Wire.write(static_cast<uint8_t>(cmd & 0xFF));
    return Wire.endTransmission() == 0;
}

static bool i2cRead(uint8_t addr, uint8_t* buf, size_t len) {
    if (Wire.requestFrom(static_cast<int>(addr), static_cast<int>(len)) != static_cast<int>(len)) return false;
    for (size_t i = 0; i < len; i++) buf[i] = Wire.read();
    return true;
}

static int readRegister(uint8_t addr, uint8_t reg) {
    Wire.beginTransmission(addr);
    Wire.write(reg);
    if (Wire.endTransmission() != 0) return -1;
    uint8_t v;
    return i2cRead(addr, &v, 1) ? v : -1;
}

// ---------- setup ----------

void Sensors::configure(const cc::Config& cfg) {
    cfg_ = cfg;
#ifdef CC_FORCE_SIMULATION
    simulate_ = true;
#else
    simulate_ = cfg.simulateSensors;
#endif
    devices_.clear();
    samples_.assign(cfg.sensors.size(), cc::SensorSample());
    if (simulate_) {
        ESP_LOGW(TAG, "sensors are SIMULATED");
        lastSimMs_ = millis();
        return;
    }
    for (const cc::SensorConfig& sc : cfg.sensors) {
        std::unique_ptr<Device> d(new Device());
        d->cfg = sc;
        // SCD41 delivers every 5 s, polling for "data ready" is cheap. BME680 reads block ~200 ms.
        d->intervalMs = sc.type == cc::SensorType::Scd41 ? 1000 : sc.type == cc::SensorType::Bme680 ? 3000 : 2000;
        devices_.push_back(std::move(d));
    }
    Wire.end();
    Wire.begin(cfg.i2c.sda, cfg.i2c.scl, cfg.i2c.freqHz);
    Wire.setTimeOut(50);
    busFailures_ = 0;
}

bool Sensors::select(int channel) {
    Wire.beginTransmission(static_cast<uint8_t>(cfg_.i2c.muxAddress));
    Wire.write(channel < 0 ? 0 : static_cast<uint8_t>(1 << channel));
    return Wire.endTransmission() == 0;
}

bool Sensors::init(Device& d) {
    const uint8_t addr = static_cast<uint8_t>(d.cfg.address);
    switch (d.cfg.type) {
        case cc::SensorType::Scd41:
            // The sensor keeps measuring across ESP reboots; it must be stopped before it accepts
            // a new start command. Stopping takes 500 ms.
            i2cCommand(addr, cc::scd41::kStopPeriodicMeasurement);
            delay(500);
            return i2cCommand(addr, cc::scd41::kStartPeriodicMeasurement);
        case cc::SensorType::Bme280:
            d.bme280.reset(new Adafruit_BME280());
            return d.bme280->begin(addr, &Wire);
        case cc::SensorType::Bme680:
            d.bme680.reset(new Adafruit_BME680(&Wire));
            return d.bme680->begin(addr);
    }
    return false;
}

// ---------- reading ----------

Sensors::Result Sensors::readScd41(Device& d, cc::SensorSample& s) {
    const uint8_t addr = static_cast<uint8_t>(d.cfg.address);
    uint8_t raw[9];
    uint16_t status;
    if (!i2cCommand(addr, cc::scd41::kGetDataReadyStatus)) return Result::Error;
    delay(2);
    if (!i2cRead(addr, raw, 3) || !cc::scd41::decodeWords(raw, 1, &status)) return Result::Error;
    if (!cc::scd41::dataReady(status)) return Result::NotReady;

    if (!i2cCommand(addr, cc::scd41::kReadMeasurement)) return Result::Error;
    delay(2);
    float co2, t, h;
    if (!i2cRead(addr, raw, 9) || !cc::scd41::decodeMeasurement(raw, co2, t, h)) return Result::Error;
    s.m = cc::Measurement();
    s.m.set(cc::Quantity::Co2, co2);
    s.m.set(cc::Quantity::Temperature, t);
    s.m.set(cc::Quantity::Humidity, h);
    return Result::Fresh;
}

Sensors::Result Sensors::read(Device& d, cc::SensorSample& s) {
    const uint8_t addr = static_cast<uint8_t>(d.cfg.address);
    switch (d.cfg.type) {
        case cc::SensorType::Scd41:
            return readScd41(d, s);
        case cc::SensorType::Bme280: {
            // The Adafruit library does not notice a missing sensor; check the chip id first.
            if (readRegister(addr, kBoschChipIdRegister) != kBme280ChipId) return Result::Error;
            float t = d.bme280->readTemperature();
            float h = d.bme280->readHumidity();
            float p = d.bme280->readPressure() / 100.0f;
            if (isnan(t) || isnan(h) || isnan(p)) return Result::Error;
            s.m = cc::Measurement();
            s.m.set(cc::Quantity::Temperature, t);
            s.m.set(cc::Quantity::Humidity, h);
            s.m.set(cc::Quantity::Pressure, p);
            return Result::Fresh;
        }
        case cc::SensorType::Bme680: {
            if (readRegister(addr, kBoschChipIdRegister) != kBme680ChipId) return Result::Error;
            if (!d.bme680->performReading()) return Result::Error;
            s.m = cc::Measurement();
            s.m.set(cc::Quantity::Temperature, d.bme680->temperature);
            s.m.set(cc::Quantity::Humidity, d.bme680->humidity);
            s.m.set(cc::Quantity::Pressure, d.bme680->pressure / 100.0f);
            s.m.set(cc::Quantity::Gas, d.bme680->gas_resistance / 1000.0f);
            return Result::Fresh;
        }
    }
    return Result::Error;
}

const std::vector<cc::SensorSample>& Sensors::poll(uint32_t nowMs, const cc::Controller& controller) {
    if (simulate_) {
        sim_.step((nowMs - lastSimMs_) / 1000.0f, controller.outputConfigs(), controller.outputs());
        lastSimMs_ = nowMs;
        sim_.fill(cfg_, nowMs, samples_);
        return samples_;
    }

    for (size_t i = 0; i < devices_.size(); i++) {
        Device& d = *devices_[i];
        cc::SensorSample& s = samples_[i];
        const bool due = d.ready ? !cc::before(nowMs, d.nextReadMs) : !cc::before(nowMs, d.nextInitMs);
        if (!due) continue;

        if (!select(d.cfg.muxChannel)) {
            s.ok = false;
            s.failures++;
            busFailures_++;
            continue;
        }
        busFailures_ = 0;

        if (!d.ready) {
            d.ready = init(d);
            d.nextInitMs = nowMs + kRetryInitMs;
            d.nextReadMs = nowMs + (d.cfg.type == cc::SensorType::Scd41 ? 5000 : 0);
            if (!d.ready) {
                s.ok = false;
                s.failures++;
                ESP_LOGW(TAG, "%s: init failed (mux %d, 0x%02x)", d.cfg.id.c_str(), d.cfg.muxChannel, d.cfg.address);
            }
            continue;
        }

        d.nextReadMs = nowMs + d.intervalMs;
        switch (read(d, s)) {
            case Result::Fresh:
                s.ok = true;
                s.failures = 0;
                s.atMs = nowMs;
                break;
            case Result::NotReady:
                break;
            case Result::Error:
                s.ok = false;
                s.failures++;
                if (s.failures >= 3) {
                    ESP_LOGW(TAG, "%s: read failed %u times, re-initialising", d.cfg.id.c_str(), s.failures);
                    d.ready = false;
                    d.nextInitMs = nowMs;
                }
                break;
        }
    }

    if (busFailures_ >= 3) {
        ESP_LOGE(TAG, "multiplexer not answering, resetting I2C bus");
        recoverBus();
    }
    return samples_;
}

void Sensors::recoverBus() {
    const int sda = cfg_.i2c.sda, scl = cfg_.i2c.scl;
    Wire.end();
    // Clock out whatever a stuck slave still wants to send, then issue a STOP.
    pinMode(sda, INPUT_PULLUP);
    pinMode(scl, OUTPUT_OPEN_DRAIN);
    for (int i = 0; i < 9; i++) {
        digitalWrite(scl, LOW);
        delayMicroseconds(5);
        digitalWrite(scl, HIGH);
        delayMicroseconds(5);
    }
    pinMode(sda, OUTPUT_OPEN_DRAIN);
    digitalWrite(sda, LOW);
    delayMicroseconds(5);
    digitalWrite(sda, HIGH);
    delayMicroseconds(5);

    Wire.begin(sda, scl, cfg_.i2c.freqHz);
    Wire.setTimeOut(50);
    for (auto& d : devices_) d->ready = false;
    busFailures_ = 0;
}
