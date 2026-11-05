#pragma once
// Change log ("audit log"): who changed what, when, through which channel. Kept on the controller
// (last kCapacity entries, persisted in flash by App) and published via MQTT for the server.

#include <cstdint>
#include <deque>
#include <string>
#include <vector>

namespace cc {

// Who triggers a change.
struct Actor {
    std::string source;  // "web", "mqtt" or "system"
    std::string user;    // login name (web), sender name if given (mqtt), empty for system
    std::string client;  // IP address of the web client, empty otherwise
};

struct Event {
    uint32_t seq = 0;     // increasing number, never reused
    int64_t epoch = 0;    // Unix time, 0 if the clock was not set
    uint32_t uptimeS = 0;
    std::string source, user, client;
    std::string action;   // boot, output, config, clock, reboot, firmware, login-failed
    std::string detail;   // human-readable, e.g. "humidifier: on for 60 min"
};

std::string eventToJson(const Event& e);
bool eventFromJson(const std::string& json, Event& e);

class EventLog {
   public:
    static constexpr size_t kCapacity = 100;
    static constexpr size_t kMaxDetail = 800;  // longer details are cut

    // Assigns the next sequence number and returns the stored event.
    const Event& add(const Actor& who, const std::string& action, const std::string& detail, int64_t epoch,
                     uint32_t uptimeS);
    // Restores an event read back from flash (keeps its sequence number).
    void restore(const Event& e);
    const std::deque<Event>& all() const { return events_; }
    std::vector<Event> since(uint32_t seq) const;  // events with a higher sequence number
    uint32_t lastSeq() const { return lastSeq_; }

   private:
    std::deque<Event> events_;
    uint32_t lastSeq_ = 0;
};

}  // namespace cc
