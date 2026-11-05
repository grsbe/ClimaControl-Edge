#include "Mqtt.h"

#include <ArduinoJson.h>

#include <cstdlib>

#include "App.h"
#include "Network.h"
#include "StatusJson.h"

static const char* TAG = "mqtt";
static const uint32_t kRetryMs = 10000;

Mqtt mqtt;

void Mqtt::begin(const cc::Config& cfg) {
    enabled_ = cfg.mqtt.enabled;
    if (!enabled_) return;
    host_ = cfg.mqtt.host;
    port_ = cfg.mqtt.port;
    user_ = cfg.mqtt.user;
    password_ = cfg.mqtt.password;
    base_ = cfg.mqtt.baseTopic;
    intervalMs_ = cfg.mqtt.publishIntervalS * 1000;
    char suffix[12];
    snprintf(suffix, sizeof suffix, "-%06llx", ESP.getEfuseMac() & 0xFFFFFF);
    clientId_ = cfg.deviceName + suffix;

    client_.setServer(host_.c_str(), port_);  // keeps the pointer, host_ must outlive the client
    client_.setBufferSize(8192);              // incoming commands; outgoing messages are streamed
    client_.setKeepAlive(30);
    client_.setSocketTimeout(5);
    client_.setCallback([this](char* topic, uint8_t* payload, unsigned int len) {
        onMessage(topic, std::string(reinterpret_cast<const char*>(payload), len));
    });
    lastAttemptMs_ = millis() - kRetryMs;
}

void Mqtt::connect() {
    lastAttemptMs_ = millis();
    const std::string will = base_ + "/availability";
    const char* user = user_.empty() ? nullptr : user_.c_str();
    const char* pass = user_.empty() ? nullptr : password_.c_str();
    if (!client_.connect(clientId_.c_str(), user, pass, will.c_str(), 1, true, "offline")) {
        ESP_LOGW(TAG, "connecting to %s:%d failed (state %d)", host_.c_str(), port_, client_.state());
        return;
    }
    ESP_LOGI(TAG, "connected to %s:%d", host_.c_str(), port_);
    publish("availability", "online", true);
    client_.subscribe((base_ + "/cmd/#").c_str(), 1);
    publishedConfigVersion_ = 0;
    publishedAlarms_.clear();
    lastPublishMs_ = millis() - intervalMs_;
}

void Mqtt::loop() {
    if (!enabled_ || !network.online()) return;
    if (!client_.connected()) {
        if (millis() - lastAttemptMs_ >= kRetryMs) connect();
        if (!client_.connected()) return;
    }
    client_.loop();

    const uint32_t version = app.configVersion();
    if (version != publishedConfigVersion_ && publish("config", cc::configToJson(app.config(), false), true))
        publishedConfigVersion_ = version;

    if (millis() - lastPublishMs_ >= intervalMs_) {
        lastPublishMs_ = millis();
        publishStatus();
    }
}

bool Mqtt::publish(const std::string& subtopic, const std::string& payload, bool retained) {
    const std::string topic = base_ + "/" + subtopic;
    if (!client_.beginPublish(topic.c_str(), payload.size(), retained)) return false;
    client_.write(reinterpret_cast<const uint8_t*>(payload.data()), payload.size());
    return client_.endPublish();
}

void Mqtt::publishStatus() {
    const cc::Snapshot s = app.snapshot();
    publish("status", statusJson(s), true);

    char value[16];
    for (const cc::Signal& sig : s.signals) {
        if (sig.valid) snprintf(value, sizeof value, "%.1f", sig.value);
        else strcpy(value, "null");
        publish("signal/" + sig.name, value, true);
    }

    JsonDocument doc;
    cc::alarmsToJson(s.alarms, doc.to<JsonArray>());
    std::string alarms;
    serializeJson(doc, alarms);
    if (alarms != publishedAlarms_ && publish("alarms", alarms, true)) publishedAlarms_ = alarms;
}

void Mqtt::reply(bool ok, const std::vector<std::string>& messages) {
    JsonDocument doc;
    doc["ok"] = ok;
    JsonArray arr = doc[ok ? "notes" : "errors"].to<JsonArray>();
    for (const std::string& m : messages) arr.add(m);
    std::string out;
    serializeJson(doc, out);
    publish("result", out, false);
}

void Mqtt::onMessage(const std::string& topic, const std::string& payload) {
    const std::string prefix = base_ + "/cmd/";
    if (topic.compare(0, prefix.size(), prefix) != 0) return;
    const std::string cmd = topic.substr(prefix.size());
    ESP_LOGI(TAG, "command %s: %s", cmd.c_str(), payload.c_str());

    if (cmd.rfind("output/", 0) == 0) {
        std::string mode = payload;
        float level = 0;
        uint32_t minutes = 0;
        char* end = nullptr;
        const float number = strtof(payload.c_str(), &end);
        if (!payload.empty() && payload[0] == '{') {
            JsonDocument d;
            if (deserializeJson(d, payload)) return reply(false, {"invalid JSON"});
            level = d["level"] | 0.0f;
            mode = d["mode"] | (d["level"].is<float>() ? "level" : "");
            minutes = d["minutes"] | 0u;
        } else if (!payload.empty() && end && *end == '\0') {
            mode = "level";
            level = number;
        }
        std::string error;
        if (app.setOutput(cmd.substr(7), mode, level, minutes, error)) reply(true, {});
        else reply(false, {error});
    } else if (cmd == "config") {
        std::vector<std::string> errors;
        bool reboot = false;
        if (app.updateConfig(payload, errors, reboot))
            reply(true, reboot ? std::vector<std::string>{"saved, reboot to apply network settings"}
                               : std::vector<std::string>{});
        else
            reply(false, errors);
    } else if (cmd == "reboot") {
        if (payload != "reboot") return reply(false, {"send the payload 'reboot' to confirm"});
        reply(true, {});
        app.requestReboot();
    } else {
        reply(false, {"unknown command '" + cmd + "'"});
    }
}
