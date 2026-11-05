#pragma once
// Owns the configuration and the control task.
//
// The control task (sensors -> fusion -> controller -> outputs, once per second) runs on its own
// FreeRTOS task with a higher priority than the network code, and never waits for WiFi or MQTT.
// The network side (web UI, MQTT) only talks to it through the thread-safe methods below.

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include <string>
#include <vector>

#include "Config.h"
#include "Controller.h"
#include "Fusion.h"
#include "Outputs.h"
#include "Sensors.h"
#include "Status.h"

#define FIRMWARE_VERSION "0.1.0"

#if CONFIG_IDF_TARGET_ESP32S3
constexpr cc::Chip kChip = cc::Chip::Esp32S3;
#else
constexpr cc::Chip kChip = cc::Chip::Esp32;
#endif

class App {
   public:
    void begin();

    cc::Config config();
    uint32_t configVersion();  // increases on every config change
    const char* configSource() const { return configSource_; }
    cc::Snapshot snapshot();
    void credentials(std::string& user, std::string& password);

    // Overlays JSON onto the current config, validates, stores and activates it.
    // WiFi and MQTT settings take effect after a reboot (rebootRequired is set).
    bool updateConfig(const std::string& json, std::vector<std::string>& errors, bool& rebootRequired);

    // mode: "auto", "on", "off" or "level" (uses level 0..1). minutes 0 = default override timeout.
    bool setOutput(const std::string& name, const std::string& mode, float level, uint32_t minutes, std::string& error);

    void setClock(int64_t epoch);
    bool clockValid();

    void requestReboot() { rebootAtMs_ = millis() + 1000; }
    bool rebootDue() const { return rebootAtMs_ && !cc::before(millis(), rebootAtMs_); }

   private:
    struct Command {
        std::string output;
        bool manual;
        float level;
        uint32_t durationS;
    };

    class Lock {
       public:
        explicit Lock(SemaphoreHandle_t m) : m_(m) { xSemaphoreTake(m_, portMAX_DELAY); }
        ~Lock() { xSemaphoreGive(m_); }

       private:
        SemaphoreHandle_t m_;
    };

    static void controlTask(void* self);
    void controlLoop();
    void loadConfig();
    bool saveConfig(const cc::Config& cfg);

    SemaphoreHandle_t mutex_ = nullptr;
    TaskHandle_t task_ = nullptr;
    cc::Config config_;
    uint32_t configVersion_ = 1;
    bool configDirty_ = false;
    const char* configSource_ = "defaults";
    cc::Snapshot snapshot_;
    std::vector<Command> pending_;
    uint32_t rebootAtMs_ = 0;

    // used by the control task only
    Outputs outputs_;
    Sensors sensors_;
    cc::Fusion fusion_;
    cc::Controller controller_;
    std::vector<cc::SensorSample> samples_;
};

extern App app;
