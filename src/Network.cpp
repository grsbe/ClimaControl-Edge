#include "Network.h"

#include <ESPmDNS.h>
#include <WiFi.h>

static const char* TAG = "net";
// While the station is not connected it scans for the network, which disturbs clients on the
// access point. So retry only every 30 s instead of continuously.
static const uint32_t kRetryMs = 30000;

Network network;

void Network::begin(const cc::Config& cfg) {
    cfg_ = cfg;
    const bool sta = !cfg.wifi.staSsid.empty();
    WiFi.persistent(false);  // credentials come from our config, not from the WiFi NVS
    WiFi.setHostname(cfg.deviceName.c_str());
    WiFi.mode(cfg.wifi.apEnabled && sta ? WIFI_AP_STA : sta ? WIFI_STA : WIFI_AP);

    if (cfg.wifi.apEnabled) {
        WiFi.softAP(cfg.wifi.apSsid.c_str(), cfg.wifi.apPassword.c_str());
        ESP_LOGI(TAG, "access point '%s' at http://%s", cfg.wifi.apSsid.c_str(), WiFi.softAPIP().toString().c_str());
    }
    if (sta) {
        WiFi.setAutoReconnect(false);
        WiFi.begin(cfg.wifi.staSsid.c_str(), cfg.wifi.staPassword.c_str());
        lastAttemptMs_ = millis();
    }
    // Starts SNTP; harmless without internet. Also sets the time zone.
    configTzTime(cfg.time.tz.c_str(), cfg.time.ntpServer.c_str());

    if (MDNS.begin(cfg.deviceName.c_str())) MDNS.addService("http", "tcp", 80);
}

void Network::loop() {
    if (cfg_.wifi.staSsid.empty()) return;
    const bool now = online();
    if (now != wasOnline_) {
        if (now) ESP_LOGI(TAG, "joined '%s' as %s", cfg_.wifi.staSsid.c_str(), WiFi.localIP().toString().c_str());
        else ESP_LOGW(TAG, "lost connection to '%s'", cfg_.wifi.staSsid.c_str());
        wasOnline_ = now;
    }
    if (!now && millis() - lastAttemptMs_ > kRetryMs) {
        lastAttemptMs_ = millis();
        WiFi.disconnect();
        WiFi.begin(cfg_.wifi.staSsid.c_str(), cfg_.wifi.staPassword.c_str());
    }
}

bool Network::online() const { return WiFi.status() == WL_CONNECTED; }

void Network::toJson(JsonObject out) const {
    if (cfg_.wifi.apEnabled) {
        out["ap"]["ssid"] = cfg_.wifi.apSsid;
        out["ap"]["ip"] = WiFi.softAPIP().toString();
        out["ap"]["clients"] = WiFi.softAPgetStationNum();
    }
    if (!cfg_.wifi.staSsid.empty()) {
        out["sta"]["ssid"] = cfg_.wifi.staSsid;
        out["sta"]["connected"] = online();
        if (online()) {
            out["sta"]["ip"] = WiFi.localIP().toString();
            out["sta"]["rssi"] = WiFi.RSSI();
        }
    }
}
