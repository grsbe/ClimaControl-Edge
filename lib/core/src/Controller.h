#pragma once
// Decides the level of every output once per tick from the fused signals, the time of day and
// manual overrides. Pure logic: the caller reads sensors before and drives the pins after.
//
// Per output, in this order:
//   1. Manual override (on/off/level), expires after overrideTimeoutS.
//   2. Otherwise the rules: the output takes the highest level of all active rules. If any rule
//      lacks its input (invalid signal, unknown time) the output goes to its failsafe level.
//   3. Safety timing: maxOnS forces the output off for cooldownS (also in manual mode);
//      minOnS/minOffS prevent short cycling (rules only, not manual or failsafe).

#include <string>
#include <vector>

#include "Config.h"
#include "Fusion.h"

namespace cc {

struct Alarm {
    std::string code;     // stable id, e.g. "signal.chamber.co2.invalid"
    std::string message;  // human-readable
    bool critical = false;
};

enum class OutputMode { Auto, Manual };

struct OutputState {
    std::string name;
    float level = 0;  // applied level 0..1 (relays: 0 or 1)
    bool on = false;
    const char* reason = "start";  // why the output has this level
    bool failsafe = false;
    OutputMode mode = OutputMode::Auto;
    float manualLevel = 0;
    bool manualExpires = false;
    uint32_t manualUntilMs = 0;
    uint32_t changedAtMs = 0;  // last on/off transition
    bool lockedOut = false;    // maxOnS tripped, cooling down
    uint32_t lockoutUntilMs = 0;
    std::vector<bool> latch;   // hysteresis state per rule
};

class Controller {
   public:
    // Keeps the state (on/off, manual overrides) of outputs whose name still exists.
    void configure(const Config& cfg, uint32_t nowMs);

    // minuteOfDay: 0..1439 local time, or -1 if the clock is not set.
    void tick(const Fusion& fusion, uint32_t nowMs, int minuteOfDay);

    // durationS 0 = use overrideTimeoutS from the config. Returns false for unknown outputs.
    bool setManual(const std::string& name, float level, uint32_t durationS, uint32_t nowMs);
    bool setAuto(const std::string& name);

    const std::vector<OutputState>& outputs() const { return state_; }
    const std::vector<OutputConfig>& outputConfigs() const { return outputs_; }
    const std::vector<Alarm>& alarms() const { return alarms_; }
    uint32_t manualRemainingS(const OutputState& st, uint32_t nowMs) const;

   private:
    struct RuleResult {
        bool valid;
        float level;
    };
    RuleResult evalRule(const Rule& rule, std::vector<bool>::reference latch, const Fusion& fusion, uint32_t nowMs,
                        int minuteOfDay) const;
    void tickOutput(size_t i, const Fusion& fusion, uint32_t nowMs, int minuteOfDay);
    void updateAlarms(const Fusion& fusion, uint32_t nowMs);
    OutputState* find(const std::string& name);

    std::vector<OutputConfig> outputs_;
    std::vector<OutputState> state_;
    std::vector<AlarmConfig> alarmConfigs_;
    std::vector<bool> alarmPending_;
    std::vector<uint32_t> alarmSinceMs_;
    std::vector<Alarm> alarms_;
    uint32_t overrideTimeoutS_ = 0;
};

}  // namespace cc
