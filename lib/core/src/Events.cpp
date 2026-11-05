#include "Events.h"

#include <ArduinoJson.h>

namespace cc {

std::string eventToJson(const Event& e) {
    JsonDocument doc;
    doc["seq"] = e.seq;
    if (e.epoch > 0) doc["epoch"] = e.epoch;
    else doc["epoch"] = nullptr;
    doc["uptimeS"] = e.uptimeS;
    doc["source"] = e.source;
    doc["user"] = e.user;
    doc["client"] = e.client;
    doc["action"] = e.action;
    doc["detail"] = e.detail;
    std::string out;
    serializeJson(doc, out);
    return out;
}

bool eventFromJson(const std::string& json, Event& e) {
    JsonDocument doc;
    if (deserializeJson(doc, json) || !doc["seq"].is<uint32_t>()) return false;
    e.seq = doc["seq"];
    e.epoch = doc["epoch"] | int64_t(0);
    e.uptimeS = doc["uptimeS"] | 0u;
    e.source = doc["source"] | "";
    e.user = doc["user"] | "";
    e.client = doc["client"] | "";
    e.action = doc["action"] | "";
    e.detail = doc["detail"] | "";
    return true;
}

const Event& EventLog::add(const Actor& who, const std::string& action, const std::string& detail, int64_t epoch,
                           uint32_t uptimeS) {
    Event e;
    e.seq = ++lastSeq_;
    e.epoch = epoch;
    e.uptimeS = uptimeS;
    e.source = who.source;
    e.user = who.user;
    e.client = who.client;
    e.action = action;
    e.detail = detail.size() > kMaxDetail ? detail.substr(0, kMaxDetail - 3) + "..." : detail;
    events_.push_back(e);
    while (events_.size() > kCapacity) events_.pop_front();
    return events_.back();
}

void EventLog::restore(const Event& e) {
    if (e.seq <= lastSeq_) return;  // out of order or duplicate
    events_.push_back(e);
    lastSeq_ = e.seq;
    while (events_.size() > kCapacity) events_.pop_front();
}

std::vector<Event> EventLog::since(uint32_t seq) const {
    std::vector<Event> out;
    for (const Event& e : events_) {
        if (e.seq > seq) out.push_back(e);
    }
    return out;
}

}  // namespace cc
