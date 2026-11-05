#pragma once
// MQTT client for the remote server. All topics live under mqtt.baseTopic (<base> below).
//
// Published by the device:
//   <base>/availability      "online" / "offline" (retained, offline is the last will)
//   <base>/status            full status JSON, every publishIntervalS (retained)
//   <base>/signal/<name>     single values, e.g. <base>/signal/chamber.co2 = 812.0 (retained)
//   <base>/alarms            JSON array of active alarms, on change (retained)
//   <base>/config            configuration without passwords, on change (retained)
//   <base>/result            answer to a command: {"ok": true} or {"ok": false, "errors": [...]}
//
// Commands to the device (do NOT publish them retained, they would be re-run on every reconnect):
//   <base>/cmd/output/<name> "auto" | "on" | "off" | "0.5" | {"mode": "on", "minutes": 30}
//   <base>/cmd/config        JSON, merged into the current config (same as the web UI)
//   <base>/cmd/reboot        "reboot"

#include <PubSubClient.h>
#include <WiFi.h>

#include <string>

#include "Config.h"

class Mqtt {
   public:
    void begin(const cc::Config& cfg);
    void loop();
    bool connected() { return client_.connected(); }

   private:
    void connect();
    void publishStatus();
    void onMessage(const std::string& topic, const std::string& payload);
    void reply(bool ok, const std::vector<std::string>& errors);
    bool publish(const std::string& subtopic, const std::string& payload, bool retained);

    WiFiClient net_;
    PubSubClient client_{net_};
    bool enabled_ = false;
    std::string host_, user_, password_, base_, clientId_;
    int port_ = 1883;
    uint32_t intervalMs_ = 10000;
    uint32_t lastAttemptMs_ = 0;
    uint32_t lastPublishMs_ = 0;
    uint32_t publishedConfigVersion_ = 0;
    std::string publishedAlarms_;
};

extern Mqtt mqtt;
