// Unit tests for lib/core. Run on the PC with:  pio test -e native
#include <unity.h>

#include "Config.h"
#include "Controller.h"
#include "Fusion.h"
#include "Pins.h"
#include "Sensirion.h"
#include "Simulation.h"
#include "Status.h"

using namespace cc;

// ---------- helpers ----------

static Config oneSensorConfig() {
    Config c = defaultConfig(Chip::Esp32);
    c.sensors.clear();
    c.outputs.clear();
    c.alarms.clear();
    SensorConfig s;
    s.id = "a";
    s.type = SensorType::Scd41;
    s.group = "chamber";
    s.muxChannel = 0;
    s.address = 0x62;
    c.sensors.push_back(s);
    s.id = "b";
    s.muxChannel = 1;
    c.sensors.push_back(s);
    return c;
}

static OutputConfig relay(const char* name, int pin, Rule rule, float failsafe = 0) {
    OutputConfig o;
    o.name = name;
    o.pin = pin;
    o.failsafe = failsafe;
    o.rules.push_back(rule);
    return o;
}

static Rule ruleAbove(const char* signal, float on, float off) {
    Rule r;
    r.type = RuleType::Above;
    r.signal = signal;
    r.on = on;
    r.off = off;
    return r;
}

static SensorSample co2Sample(float co2, uint32_t atMs) {
    SensorSample s;
    s.ok = true;
    s.atMs = atMs;
    s.m.set(Quantity::Co2, co2);
    s.m.set(Quantity::Temperature, 20);
    s.m.set(Quantity::Humidity, 85);
    return s;
}

struct Rig {
    Config cfg;
    Fusion fusion;
    Controller ctl;
    std::vector<SensorSample> samples;

    explicit Rig(const Config& c) : cfg(c) {
        fusion.configure(cfg);
        ctl.configure(cfg, 0);
        samples.resize(cfg.sensors.size());
    }
    void co2(float value, uint32_t nowMs) {
        for (auto& s : samples) s = co2Sample(value, nowMs);
    }
    const OutputState& tick(uint32_t nowMs, int minuteOfDay = 600) {
        fusion.update(samples, nowMs);
        ctl.tick(fusion, nowMs, minuteOfDay);
        return ctl.outputs()[0];
    }
};

static bool hasAlarm(const Controller& ctl, const std::string& code) {
    for (const Alarm& a : ctl.alarms()) {
        if (a.code == code) return true;
    }
    return false;
}

// ---------- SCD41 protocol ----------

void test_crc8_datasheet_example() {
    const uint8_t data[2] = {0xBE, 0xEF};
    TEST_ASSERT_EQUAL_HEX8(0x92, scd41::crc8(data, 2));
}

void test_scd41_decode() {
    // co2 = 0x01F4 (500 ppm), temperature raw 0x6667 (~25 degC), humidity raw 0x5EB9 (~37 %)
    uint8_t raw[9] = {0x01, 0xF4, 0, 0x66, 0x67, 0, 0x5E, 0xB9, 0};
    raw[2] = scd41::crc8(raw, 2);
    raw[5] = scd41::crc8(raw + 3, 2);
    raw[8] = scd41::crc8(raw + 6, 2);
    float co2, t, h;
    TEST_ASSERT_TRUE(scd41::decodeMeasurement(raw, co2, t, h));
    TEST_ASSERT_EQUAL_FLOAT(500, co2);
    TEST_ASSERT_FLOAT_WITHIN(0.1, 25.0, t);
    TEST_ASSERT_FLOAT_WITHIN(0.1, 37.0, h);
    raw[4] ^= 1;  // corrupt
    TEST_ASSERT_FALSE(scd41::decodeMeasurement(raw, co2, t, h));
}

// ---------- pins ----------

void test_pins() {
    TEST_ASSERT_NULL(pinProblem(Chip::Esp32, 16, PinUse::Output));
    TEST_ASSERT_NOT_NULL(pinProblem(Chip::Esp32, 6, PinUse::Output));    // flash
    TEST_ASSERT_NOT_NULL(pinProblem(Chip::Esp32, 12, PinUse::Output));   // strapping
    TEST_ASSERT_NOT_NULL(pinProblem(Chip::Esp32, 34, PinUse::Output));   // input only
    TEST_ASSERT_NULL(pinProblem(Chip::Esp32, 34, PinUse::Input));
    TEST_ASSERT_NOT_NULL(pinProblem(Chip::Esp32S3, 22, PinUse::Output));  // does not exist
    TEST_ASSERT_NULL(pinProblem(Chip::Esp32S3, 4, PinUse::Output));
}

// ---------- config ----------

static void assertNoErrors(const std::vector<std::string>& errors) {
    std::string all;
    for (const auto& e : errors) all += e + "\n";
    TEST_ASSERT_EQUAL_MESSAGE(0, errors.size(), all.c_str());
}

void test_default_config_is_valid() {
    assertNoErrors(validateConfig(defaultConfig(Chip::Esp32), Chip::Esp32));
    assertNoErrors(validateConfig(defaultConfig(Chip::Esp32S3), Chip::Esp32S3));
}

void test_config_json_roundtrip() {
    Config a = defaultConfig(Chip::Esp32);
    a.mqtt.password = "secret";
    a.sensors[0].offset[idx(Quantity::Temperature)] = -1.5f;
    std::string json = configToJson(a, true);

    Config b;
    std::vector<std::string> errors;
    TEST_ASSERT_TRUE(parseConfig(json, b, errors));
    assertNoErrors(errors);
    TEST_ASSERT_EQUAL_STRING(json.c_str(), configToJson(b, true).c_str());
    TEST_ASSERT_EQUAL_STRING("secret", b.mqtt.password.c_str());
    TEST_ASSERT_EQUAL_FLOAT(-1.5f, b.sensors[0].offset[idx(Quantity::Temperature)]);
}

void test_masked_secret_keeps_password() {
    Config cfg = defaultConfig(Chip::Esp32);
    cfg.mqtt.password = "secret";
    std::string masked = configToJson(cfg, false);
    TEST_ASSERT_EQUAL(std::string::npos, masked.find("secret"));

    std::vector<std::string> errors;
    TEST_ASSERT_TRUE(parseConfig(masked, cfg, errors));
    TEST_ASSERT_EQUAL_STRING("secret", cfg.mqtt.password.c_str());
}

void test_partial_update_keeps_other_fields() {
    Config cfg = defaultConfig(Chip::Esp32);
    std::vector<std::string> errors;
    TEST_ASSERT_TRUE(parseConfig(R"({"mqtt": {"host": "10.0.0.5"}})", cfg, errors));
    TEST_ASSERT_EQUAL_STRING("10.0.0.5", cfg.mqtt.host.c_str());
    TEST_ASSERT_EQUAL(1883, cfg.mqtt.port);
    TEST_ASSERT_EQUAL(8, cfg.outputs.size());
}

void test_parse_reports_type_errors() {
    Config cfg = defaultConfig(Chip::Esp32);
    std::vector<std::string> errors;
    TEST_ASSERT_FALSE(parseConfig(R"({"mqtt": {"port": "abc"}, "outputs": [{"name": "x", "kind": "dimmer"}]})", cfg,
                                  errors));
    TEST_ASSERT_EQUAL(2, errors.size());
    errors.clear();
    TEST_ASSERT_FALSE(parseConfig("{not json", cfg, errors));
}

void test_validation_catches_mistakes() {
    Config cfg = defaultConfig(Chip::Esp32);
    cfg.outputs[0].pin = 12;                         // strapping pin
    cfg.outputs[1].pin = cfg.outputs[2].pin;          // duplicate
    cfg.outputs[3].rules[0].windows.clear();          // schedule without windows
    cfg.outputs[4].rules[0].signal = "chamber.ozone"; // unknown
    cfg.outputs[5].rules[0].off = 2000;               // above rule with off > on
    auto errors = validateConfig(cfg, Chip::Esp32);
    TEST_ASSERT_EQUAL(5, errors.size());
}

// ---------- fusion ----------

void test_fusion_median_and_disagreement() {
    Config cfg = oneSensorConfig();
    SensorConfig s = cfg.sensors[0];
    s.id = "c";
    s.muxChannel = 2;
    cfg.sensors.push_back(s);
    Fusion f;
    f.configure(cfg);
    std::vector<SensorSample> samples = {co2Sample(800, 1000), co2Sample(820, 1000), co2Sample(1500, 1000)};
    f.update(samples, 1000);
    const Signal* co2 = f.find("chamber.co2");
    TEST_ASSERT_NOT_NULL(co2);
    TEST_ASSERT_TRUE(co2->valid);
    TEST_ASSERT_EQUAL_FLOAT(820, co2->value);  // median ignores the outlier
    TEST_ASSERT_TRUE(co2->disagree);
    TEST_ASSERT_EQUAL(3, co2->sources);
}

void test_fusion_drops_stale_and_implausible() {
    Config cfg = oneSensorConfig();
    Fusion f;
    f.configure(cfg);
    std::vector<SensorSample> samples = {co2Sample(800, 0), co2Sample(-5, 40000)};
    f.update(samples, 40000);  // first is 40 s old (> staleS 30), second is negative
    const Signal* co2 = f.find("chamber.co2");
    TEST_ASSERT_FALSE(co2->valid);
    TEST_ASSERT_EQUAL(0, co2->sources);
    TEST_ASSERT_EQUAL(2, co2->expected);
}

void test_calibration_offset() {
    Config cfg = oneSensorConfig();
    cfg.sensors[0].offset[idx(Quantity::Temperature)] = -2;
    std::vector<SensorSample> samples = {co2Sample(800, 0), co2Sample(800, 0)};
    applyCalibration(cfg, samples);
    TEST_ASSERT_EQUAL_FLOAT(18, samples[0].m.value[idx(Quantity::Temperature)]);
    TEST_ASSERT_EQUAL_FLOAT(20, samples[1].m.value[idx(Quantity::Temperature)]);
}

// ---------- controller ----------

void test_hysteresis() {
    Config cfg = oneSensorConfig();
    cfg.outputs.push_back(relay("fan", 16, ruleAbove("chamber.co2", 1000, 800)));
    Rig rig(cfg);
    rig.co2(900, 1000);
    TEST_ASSERT_FALSE(rig.tick(1000).on);
    rig.co2(1100, 2000);
    TEST_ASSERT_TRUE(rig.tick(2000).on);
    rig.co2(900, 3000);
    TEST_ASSERT_TRUE(rig.tick(3000).on);  // between thresholds: stays on
    rig.co2(790, 4000);
    TEST_ASSERT_FALSE(rig.tick(4000).on);
}

void test_failsafe_when_sensors_missing() {
    Config cfg = oneSensorConfig();
    cfg.outputs.push_back(relay("fan", 16, ruleAbove("chamber.co2", 1000, 800), 1));
    Rig rig(cfg);
    rig.co2(500, 1000);
    TEST_ASSERT_FALSE(rig.tick(1000).on);
    // no new readings for longer than staleS
    const OutputState& st = rig.tick(1000 + 31000);
    TEST_ASSERT_TRUE(st.on);
    TEST_ASSERT_TRUE(st.failsafe);
    TEST_ASSERT_TRUE(hasAlarm(rig.ctl, "signal.chamber.co2.invalid"));
    TEST_ASSERT_TRUE(hasAlarm(rig.ctl, "output.fan.failsafe"));
}

void test_one_sensor_down_is_degraded_not_failsafe() {
    Config cfg = oneSensorConfig();
    cfg.outputs.push_back(relay("fan", 16, ruleAbove("chamber.co2", 1000, 800), 1));
    Rig rig(cfg);
    rig.samples[0] = co2Sample(500, 1000);
    rig.samples[1].ok = false;
    const OutputState& st = rig.tick(1000);
    TEST_ASSERT_FALSE(st.on);
    TEST_ASSERT_FALSE(st.failsafe);
    TEST_ASSERT_TRUE(hasAlarm(rig.ctl, "signal.chamber.co2.degraded"));
}

void test_schedule_wraps_midnight_and_needs_clock() {
    Config cfg = oneSensorConfig();
    Rule r;
    r.type = RuleType::Schedule;
    r.windows.push_back({22 * 60, 6 * 60});
    cfg.outputs.push_back(relay("light", 16, r));
    Rig rig(cfg);
    TEST_ASSERT_TRUE(rig.tick(1000, 23 * 60).on);
    TEST_ASSERT_TRUE(rig.tick(2000, 2 * 60).on);
    TEST_ASSERT_FALSE(rig.tick(3000, 12 * 60).on);
    const OutputState& st = rig.tick(4000, -1);
    TEST_ASSERT_TRUE(st.failsafe);
    TEST_ASSERT_FALSE(st.on);
}

void test_cycle() {
    Config cfg = oneSensorConfig();
    Rule r;
    r.type = RuleType::Cycle;
    r.onS = 60;
    r.periodS = 600;
    cfg.outputs.push_back(relay("vent", 16, r));
    Rig rig(cfg);
    TEST_ASSERT_TRUE(rig.tick(30000).on);
    TEST_ASSERT_FALSE(rig.tick(90000).on);
    TEST_ASSERT_TRUE(rig.tick(610000).on);
}

void test_linear_pwm() {
    Config cfg = oneSensorConfig();
    Rule r;
    r.type = RuleType::Linear;
    r.signal = "chamber.co2";
    r.inMin = 800;
    r.inMax = 1600;
    r.outMin = 0.2f;
    r.outMax = 1.0f;
    OutputConfig o = relay("fan", 25, r);
    o.kind = OutputKind::Pwm;
    cfg.outputs.push_back(o);
    Rig rig(cfg);
    rig.co2(1200, 1000);
    TEST_ASSERT_FLOAT_WITHIN(0.001, 0.6f, rig.tick(1000).level);
    rig.co2(5000, 2000);
    TEST_ASSERT_FLOAT_WITHIN(0.001, 1.0f, rig.tick(2000).level);
}

void test_min_on_and_min_off() {
    Config cfg = oneSensorConfig();
    OutputConfig o = relay("fan", 16, ruleAbove("chamber.co2", 1000, 800));
    o.minOnS = 60;
    o.minOffS = 30;
    cfg.outputs.push_back(o);
    Rig rig(cfg);
    rig.co2(1100, 40000);
    TEST_ASSERT_TRUE(rig.tick(40000).on);  // started off at t=0, 40 s > minOffS
    rig.co2(500, 50000);
    const OutputState& st = rig.tick(50000);
    TEST_ASSERT_TRUE(st.on);  // only 10 s on, minOnS 60
    TEST_ASSERT_EQUAL_STRING("min-on", st.reason);
    rig.co2(500, 100000);
    TEST_ASSERT_FALSE(rig.tick(100000).on);
    rig.co2(1100, 110000);
    TEST_ASSERT_FALSE(rig.tick(110000).on);  // only 10 s off, minOffS 30
    rig.co2(1100, 131000);
    TEST_ASSERT_TRUE(rig.tick(131000).on);
}

void test_max_on_forces_cooldown() {
    Config cfg = oneSensorConfig();
    Rule r;
    r.type = RuleType::Below;
    r.signal = "chamber.humidity";
    r.on = 90;
    r.off = 95;
    OutputConfig o = relay("humidifier", 16, r);
    o.maxOnS = 100;
    o.cooldownS = 50;
    cfg.outputs.push_back(o);
    Rig rig(cfg);
    for (uint32_t t = 1000; t <= 100000; t += 1000) {
        rig.co2(800, t);  // humidity stays at 85, rule wants on
        rig.tick(t);
    }
    rig.co2(800, 101000);
    const OutputState& st = rig.tick(101000);
    TEST_ASSERT_FALSE(st.on);
    TEST_ASSERT_EQUAL_STRING("max-on", st.reason);
    TEST_ASSERT_TRUE(hasAlarm(rig.ctl, "output.humidifier.max_on"));
    rig.co2(800, 140000);
    TEST_ASSERT_FALSE(rig.tick(140000).on);
    rig.co2(800, 152000);
    TEST_ASSERT_TRUE(rig.tick(152000).on);
}

void test_manual_override_expires() {
    Config cfg = oneSensorConfig();
    cfg.overrideTimeoutS = 60;
    cfg.outputs.push_back(relay("fan", 16, ruleAbove("chamber.co2", 1000, 800)));
    Rig rig(cfg);
    rig.co2(500, 1000);
    TEST_ASSERT_TRUE(rig.ctl.setManual("fan", 1, 0, 1000));
    TEST_ASSERT_FALSE(rig.ctl.setManual("nope", 1, 0, 1000));
    TEST_ASSERT_TRUE(rig.tick(1000).on);
    TEST_ASSERT_EQUAL(60, rig.ctl.manualRemainingS(rig.ctl.outputs()[0], 1000));
    rig.co2(500, 60000);
    TEST_ASSERT_TRUE(rig.tick(60000).on);
    rig.co2(500, 62000);
    const OutputState& st = rig.tick(62000);
    TEST_ASSERT_FALSE(st.on);
    TEST_ASSERT_TRUE(st.mode == OutputMode::Auto);
}

void test_reconfigure_keeps_state() {
    Config cfg = oneSensorConfig();
    cfg.outputs.push_back(relay("fan", 16, ruleAbove("chamber.co2", 1000, 800)));
    Rig rig(cfg);
    rig.ctl.setManual("fan", 1, 0, 0);
    rig.tick(1000);
    cfg.outputs[0].rules[0].on = 1200;
    rig.ctl.configure(cfg, 2000);
    TEST_ASSERT_TRUE(rig.ctl.outputs()[0].mode == OutputMode::Manual);
    TEST_ASSERT_TRUE(rig.ctl.outputs()[0].on);
}

void test_limit_alarm_delay() {
    Config cfg = oneSensorConfig();
    AlarmConfig a;
    a.signal = "chamber.co2";
    a.max = 2000;
    a.delayS = 60;
    cfg.alarms.push_back(a);
    Rig rig(cfg);
    rig.co2(2500, 1000);
    rig.tick(1000);
    TEST_ASSERT_FALSE(hasAlarm(rig.ctl, "limit.chamber.co2.high"));
    rig.co2(2500, 61000);
    rig.tick(61000);
    TEST_ASSERT_TRUE(hasAlarm(rig.ctl, "limit.chamber.co2.high"));
    rig.co2(1000, 62000);
    rig.tick(62000);
    TEST_ASSERT_FALSE(hasAlarm(rig.ctl, "limit.chamber.co2.high"));
}

void test_millis_wraparound() {
    Config cfg = oneSensorConfig();
    cfg.overrideTimeoutS = 60;
    cfg.outputs.push_back(relay("fan", 16, ruleAbove("chamber.co2", 1000, 800)));
    Rig rig(cfg);
    const uint32_t start = 0xFFFFFFFFu - 10000;  // 10 s before millis() overflows
    rig.co2(500, start);
    rig.ctl.setManual("fan", 1, 0, start);
    TEST_ASSERT_TRUE(rig.tick(start).on);
    rig.co2(500, start + 30000);
    TEST_ASSERT_TRUE(rig.tick(start + 30000).on);  // wrapped, still within 60 s
    rig.co2(500, start + 61000);
    TEST_ASSERT_FALSE(rig.tick(start + 61000).on);
}

// ---------- whole loop with the simulation ----------

void test_default_config_against_simulation() {
    Config cfg = defaultConfig(Chip::Esp32);
    Fusion fusion;
    Controller ctl;
    Simulation sim;
    sim.humidity = 70;
    fusion.configure(cfg);
    ctl.configure(cfg, 0);
    std::vector<SensorSample> samples;
    float minHumidity = 100, maxHumidity = 0, maxCo2 = 0;
    for (uint32_t t = 1000; t <= 4 * 3600 * 1000u; t += 1000) {
        sim.fill(cfg, t, samples);
        fusion.update(samples, t);
        ctl.tick(fusion, t, (t / 60000) % 1440);
        sim.step(1, ctl.outputConfigs(), ctl.outputs());
        if (t > 3600 * 1000u) {  // after one hour of settling
            minHumidity = std::min(minHumidity, sim.humidity);
            maxHumidity = std::max(maxHumidity, sim.humidity);
            maxCo2 = std::max(maxCo2, sim.co2);
        }
    }
    TEST_ASSERT_TRUE_MESSAGE(minHumidity > 80, "humidity fell below 80 %");
    TEST_ASSERT_TRUE_MESSAGE(maxHumidity < 97, "humidity rose above 97 %");
    TEST_ASSERT_TRUE_MESSAGE(maxCo2 < 1300, "CO2 rose above 1300 ppm");

    JsonDocument doc;
    snapshotToJson(makeSnapshot(cfg, samples, fusion, ctl, 4 * 3600 * 1000u, 0), doc.to<JsonObject>());
    TEST_ASSERT_EQUAL(8, doc["outputs"].size());
    TEST_ASSERT_TRUE(doc["signals"]["chamber.co2"]["value"].is<float>());
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_crc8_datasheet_example);
    RUN_TEST(test_scd41_decode);
    RUN_TEST(test_pins);
    RUN_TEST(test_default_config_is_valid);
    RUN_TEST(test_config_json_roundtrip);
    RUN_TEST(test_masked_secret_keeps_password);
    RUN_TEST(test_partial_update_keeps_other_fields);
    RUN_TEST(test_parse_reports_type_errors);
    RUN_TEST(test_validation_catches_mistakes);
    RUN_TEST(test_fusion_median_and_disagreement);
    RUN_TEST(test_fusion_drops_stale_and_implausible);
    RUN_TEST(test_calibration_offset);
    RUN_TEST(test_hysteresis);
    RUN_TEST(test_failsafe_when_sensors_missing);
    RUN_TEST(test_one_sensor_down_is_degraded_not_failsafe);
    RUN_TEST(test_schedule_wraps_midnight_and_needs_clock);
    RUN_TEST(test_cycle);
    RUN_TEST(test_linear_pwm);
    RUN_TEST(test_min_on_and_min_off);
    RUN_TEST(test_max_on_forces_cooldown);
    RUN_TEST(test_manual_override_expires);
    RUN_TEST(test_reconfigure_keeps_state);
    RUN_TEST(test_limit_alarm_delay);
    RUN_TEST(test_millis_wraparound);
    RUN_TEST(test_default_config_against_simulation);
    return UNITY_END();
}
