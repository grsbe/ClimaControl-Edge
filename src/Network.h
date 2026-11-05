#pragma once
// WiFi: the device always opens its own access point (for people on site) and, if configured,
// also joins the farm network (for MQTT and NTP time).

#include <ArduinoJson.h>

#include "Config.h"

class Network {
   public:
    void begin(const cc::Config& cfg);
    void loop();
    bool online() const;  // joined the farm network
    void toJson(JsonObject out) const;

   private:
    cc::Config cfg_;
    uint32_t lastAttemptMs_ = 0;
    bool wasOnline_ = false;
};

extern Network network;
