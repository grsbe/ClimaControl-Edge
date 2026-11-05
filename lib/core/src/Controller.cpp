#include "Controller.h"

#include <algorithm>
#include <cstdio>

namespace cc {

static std::string fmt(float v) {
    char buf[16];
    snprintf(buf, sizeof buf, "%.1f", v);
    return buf;
}

void Controller::configure(const Config& cfg, uint32_t nowMs) {
    std::vector<OutputState> next;
    for (const OutputConfig& oc : cfg.outputs) {
        OutputState st;
        if (OutputState* old = find(oc.name)) {
            st = *old;
        } else {
            st.name = oc.name;
            st.level = oc.kind == OutputKind::Relay && oc.failsafe > 0 ? 1.0f : oc.failsafe;
            st.on = st.level > 0;
            st.changedAtMs = nowMs;
        }
        st.latch.assign(oc.rules.size(), false);
        next.push_back(st);
    }
    state_ = std::move(next);
    outputs_ = cfg.outputs;
    overrideTimeoutS_ = cfg.overrideTimeoutS;
    alarmConfigs_ = cfg.alarms;
    alarmPending_.assign(cfg.alarms.size(), false);
    alarmSinceMs_.assign(cfg.alarms.size(), 0);
}

OutputState* Controller::find(const std::string& name) {
    for (OutputState& st : state_) {
        if (st.name == name) return &st;
    }
    return nullptr;
}

bool Controller::setManual(const std::string& name, float level, uint32_t durationS, uint32_t nowMs) {
    OutputState* st = find(name);
    if (!st) return false;
    if (durationS == 0) durationS = overrideTimeoutS_;
    st->mode = OutputMode::Manual;
    st->manualLevel = std::min(1.0f, std::max(0.0f, level));
    st->manualExpires = durationS > 0;
    st->manualUntilMs = nowMs + durationS * 1000;
    return true;
}

bool Controller::setAuto(const std::string& name) {
    OutputState* st = find(name);
    if (!st) return false;
    st->mode = OutputMode::Auto;
    return true;
}

uint32_t Controller::manualRemainingS(const OutputState& st, uint32_t nowMs) const {
    if (st.mode != OutputMode::Manual || !st.manualExpires || !before(nowMs, st.manualUntilMs)) return 0;
    return (st.manualUntilMs - nowMs + 999) / 1000;
}

Controller::RuleResult Controller::evalRule(const Rule& rule, std::vector<bool>::reference latch,
                                            const Fusion& fusion, uint32_t nowMs, int minuteOfDay) const {
    switch (rule.type) {
        case RuleType::Schedule: {
            if (minuteOfDay < 0) return {false, 0};
            for (const TimeWindow& w : rule.windows) {
                bool inside = w.fromMin < w.toMin ? minuteOfDay >= w.fromMin && minuteOfDay < w.toMin
                                                  : minuteOfDay >= w.fromMin || minuteOfDay < w.toMin;
                if (inside) return {true, rule.level};
            }
            return {true, 0};
        }
        case RuleType::Cycle: {
            uint32_t phase = (nowMs / 1000) % rule.periodS;
            return {true, phase < rule.onS ? rule.level : 0};
        }
        default:
            break;
    }

    const Signal* sig = fusion.find(rule.signal);
    if (!sig || !sig->valid) return {false, 0};
    const float v = sig->value;
    switch (rule.type) {
        case RuleType::Above:
            if (v >= rule.on) latch = true;
            else if (v <= rule.off) latch = false;
            return {true, latch ? rule.level : 0};
        case RuleType::Below:
            if (v <= rule.on) latch = true;
            else if (v >= rule.off) latch = false;
            return {true, latch ? rule.level : 0};
        case RuleType::Linear: {
            float t = (v - rule.inMin) / (rule.inMax - rule.inMin);
            t = std::min(1.0f, std::max(0.0f, t));
            return {true, rule.outMin + t * (rule.outMax - rule.outMin)};
        }
        default:
            return {false, 0};
    }
}

void Controller::tickOutput(size_t i, const Fusion& fusion, uint32_t nowMs, int minuteOfDay) {
    const OutputConfig& oc = outputs_[i];
    OutputState& st = state_[i];

    if (st.mode == OutputMode::Manual && st.manualExpires && !before(nowMs, st.manualUntilMs)) st.mode = OutputMode::Auto;

    float level = 0;
    const char* reason = "auto";
    bool forced = false;  // manual and failsafe levels skip minOnS/minOffS
    st.failsafe = false;
    if (st.mode == OutputMode::Manual) {
        level = st.manualLevel;
        reason = "manual";
        forced = true;
    } else if (oc.rules.empty()) {
        reason = "no rules";
    } else {
        bool anyInvalid = false;
        for (size_t j = 0; j < oc.rules.size(); j++) {
            RuleResult r = evalRule(oc.rules[j], st.latch[j], fusion, nowMs, minuteOfDay);
            if (!r.valid) anyInvalid = true;
            else level = std::max(level, r.level);
        }
        if (anyInvalid) {
            level = oc.failsafe;
            reason = "failsafe";
            st.failsafe = true;
            forced = true;
        }
    }
    if (oc.kind == OutputKind::Relay) level = level > 0 ? 1.0f : 0.0f;

    const bool wantOn = level > 0;
    const uint32_t sinceChange = nowMs - st.changedAtMs;
    if (st.lockedOut && before(nowMs, st.lockoutUntilMs)) {
        level = 0;
        reason = "max-on cooldown";
    } else {
        st.lockedOut = false;
        if (st.on && wantOn && oc.maxOnS > 0 && sinceChange >= oc.maxOnS * 1000) {
            level = 0;
            reason = "max-on";
            st.lockedOut = true;
            st.lockoutUntilMs = nowMs + oc.cooldownS * 1000;
        } else if (!forced) {
            if (st.on && !wantOn && sinceChange < oc.minOnS * 1000) {
                level = st.level;
                reason = "min-on";
            } else if (!st.on && wantOn && sinceChange < oc.minOffS * 1000) {
                level = 0;
                reason = "min-off";
            }
        }
    }

    const bool on = level > 0;
    if (on != st.on) {
        st.on = on;
        st.changedAtMs = nowMs;
    }
    st.level = level;
    st.reason = reason;
}

void Controller::tick(const Fusion& fusion, uint32_t nowMs, int minuteOfDay) {
    for (size_t i = 0; i < outputs_.size(); i++) tickOutput(i, fusion, nowMs, minuteOfDay);
    updateAlarms(fusion, nowMs);
}

void Controller::updateAlarms(const Fusion& fusion, uint32_t nowMs) {
    alarms_.clear();
    for (const Signal& s : fusion.signals()) {
        if (!s.valid) {
            alarms_.push_back({"signal." + s.name + ".invalid", "No valid reading for " + s.name, true});
            continue;
        }
        if (s.sources < s.expected) {
            alarms_.push_back({"signal." + s.name + ".degraded",
                               s.name + ": only " + std::to_string(s.sources) + " of " + std::to_string(s.expected) +
                                   " sensors deliver data",
                               false});
        }
        if (s.disagree) alarms_.push_back({"signal." + s.name + ".disagree", s.name + ": sensors disagree", false});
    }

    for (size_t i = 0; i < state_.size(); i++) {
        const OutputState& st = state_[i];
        if (st.failsafe)
            alarms_.push_back({"output." + st.name + ".failsafe", st.name + " runs in failsafe (input missing)", true});
        if (st.lockedOut)
            alarms_.push_back({"output." + st.name + ".max_on",
                               st.name + " was on longer than " + std::to_string(outputs_[i].maxOnS) + " s, forced off",
                               true});
    }

    for (size_t i = 0; i < alarmConfigs_.size(); i++) {
        const AlarmConfig& a = alarmConfigs_[i];
        const Signal* s = fusion.find(a.signal);
        bool low = s && s->valid && !std::isnan(a.min) && s->value < a.min;
        bool high = s && s->valid && !std::isnan(a.max) && s->value > a.max;
        if (!low && !high) {
            alarmPending_[i] = false;
            continue;
        }
        if (!alarmPending_[i]) {
            alarmPending_[i] = true;
            alarmSinceMs_[i] = nowMs;
        }
        if (nowMs - alarmSinceMs_[i] >= a.delayS * 1000) {
            std::string msg = a.signal + " is " + fmt(s->value) + (low ? ", below " + fmt(a.min) : ", above " + fmt(a.max));
            alarms_.push_back({"limit." + a.signal + (low ? ".low" : ".high"), msg, true});
        }
    }
}

}  // namespace cc
