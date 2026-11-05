#include "App.h"

#include <LittleFS.h>
#include <esp_task_wdt.h>
#include <sys/time.h>

#include <ctime>

#include "StatusJson.h"

static const char* TAG = "app";
static const char* kConfigPath = "/config.json";
static const char* kBackupPath = "/config.bak.json";
static const char* kTempPath = "/config.tmp.json";
static const char* kEventsPath = "/events.log";         // one JSON event per line, appended
static const char* kEventsTempPath = "/events.tmp";
static const size_t kEventsFileMax = 48 * 1024;         // rewritten with the last 100 events beyond this
static const uint32_t kTickMs = 1000;
static const uint32_t kWatchdogS = 15;
static const time_t kMinValidEpoch = 1700000000;  // anything earlier means the clock was never set

App app;

// ---------- config storage ----------

static bool readFile(const char* path, std::string& out) {
    File f = LittleFS.open(path, "r");
    if (!f) return false;
    out.clear();
    char buf[256];
    while (f.available()) {
        size_t n = f.read(reinterpret_cast<uint8_t*>(buf), sizeof buf);
        out.append(buf, n);
    }
    f.close();
    return true;
}

static bool loadFrom(const char* path, cc::Config& cfg) {
    std::string json;
    if (!LittleFS.exists(path) || !readFile(path, json)) return false;
    cc::Config c = cc::defaultConfig(kChip);
    std::vector<std::string> errors;
    if (cc::parseConfig(json, c, errors)) errors = cc::validateConfig(c, kChip);
    for (const std::string& e : errors) ESP_LOGE(TAG, "%s: %s", path, e.c_str());
    if (!errors.empty()) return false;
    cfg = c;
    return true;
}

void App::loadConfig() {
    config_ = cc::defaultConfig(kChip);
    if (!LittleFS.begin(true)) {
        ESP_LOGE(TAG, "LittleFS mount failed, running with factory defaults");
        return;
    }
    if (loadFrom(kConfigPath, config_)) {
        configSource_ = "flash";
    } else if (loadFrom(kBackupPath, config_)) {
        configSource_ = "backup";
        ESP_LOGW(TAG, "config.json missing or invalid, using the previous config");
    } else {
        configSource_ = "defaults";
        ESP_LOGW(TAG, "no valid config stored, using factory defaults");
        if (!LittleFS.exists(kConfigPath)) saveConfig(config_);
    }
#ifdef CC_WOKWI
    // The Wokwi simulator has no access point mode, only its own open "Wokwi-GUEST" network.
    config_.wifi.apEnabled = false;
    config_.wifi.staSsid = "Wokwi-GUEST";
    config_.wifi.staPassword = "";
#endif
}

// Write to a temp file and rename, so a power cut never leaves a half-written config.
bool App::saveConfig(const cc::Config& cfg) {
    const std::string json = cc::configToJson(cfg, true);
    File f = LittleFS.open(kTempPath, "w");
    if (!f) return false;
    const size_t written = f.write(reinterpret_cast<const uint8_t*>(json.data()), json.size());
    f.close();
    if (written != json.size()) {
        LittleFS.remove(kTempPath);
        return false;
    }
    if (LittleFS.exists(kConfigPath)) {
        LittleFS.remove(kBackupPath);
        LittleFS.rename(kConfigPath, kBackupPath);
    }
    return LittleFS.rename(kTempPath, kConfigPath);
}

// ---------- change log storage ----------

static int64_t nowEpoch() {
    const time_t t = time(nullptr);
    return t > kMinValidEpoch ? static_cast<int64_t>(t) : 0;
}

void App::loadEvents() {
    File f = LittleFS.open(kEventsPath, "r");
    if (!f) return;
    while (f.available()) {
        String line = f.readStringUntil('\n');
        cc::Event e;
        if (cc::eventFromJson(line.c_str(), e)) events_.restore(e);
    }
    f.close();
}

// Appends one line. When the file grows too large it is rewritten with the events still in memory
// (temp file + rename, so a power cut cannot leave it half-written).
void App::persistEvent(const cc::Event& e) {
    File f = LittleFS.open(kEventsPath, "a");
    if (!f) return;
    const std::string line = cc::eventToJson(e) + "\n";
    f.write(reinterpret_cast<const uint8_t*>(line.data()), line.size());
    const size_t size = f.size();
    f.close();
    if (size <= kEventsFileMax) return;
    File t = LittleFS.open(kEventsTempPath, "w");
    if (!t) return;
    for (const cc::Event& ev : events_.all()) {
        const std::string l = cc::eventToJson(ev) + "\n";
        t.write(reinterpret_cast<const uint8_t*>(l.data()), l.size());
    }
    t.close();
    LittleFS.remove(kEventsPath);
    LittleFS.rename(kEventsTempPath, kEventsPath);
}

void App::logEvent(const cc::Actor& who, const std::string& action, const std::string& detail) {
    Lock lock(mutex_);
    const cc::Event& e = events_.add(who, action, detail, nowEpoch(), millis() / 1000);
    ESP_LOGI(TAG, "event %u: %s %s/%s: %s", (unsigned)e.seq, e.action.c_str(), e.source.c_str(), e.user.c_str(),
             e.detail.c_str());
    persistEvent(e);
}

std::vector<cc::Event> App::events() {
    Lock lock(mutex_);
    return std::vector<cc::Event>(events_.all().begin(), events_.all().end());
}

std::vector<cc::Event> App::eventsSince(uint32_t seq) {
    Lock lock(mutex_);
    return events_.since(seq);
}

// ---------- startup ----------

void App::begin() {
    mutex_ = xSemaphoreCreateMutex();
    loadConfig();
    loadEvents();
    bootEventSeq_ = events_.lastSeq() + 1;
    logEvent({"system", "", ""}, "boot",
             std::string("firmware " FIRMWARE_VERSION ", reset reason: ") + resetReasonName() + ", config from " +
                 configSource_);
    // Until this point the output pins float; the SSR board needs pull-downs to stay off.
    outputs_.configure(config_);
    ESP_LOGI(TAG, "config from %s, %u sensors, %u outputs", configSource_, (unsigned)config_.sensors.size(),
             (unsigned)config_.outputs.size());

    setenv("TZ", config_.time.tz.c_str(), 1);
    tzset();

    sensors_.configure(config_);
    fusion_.configure(config_);
    controller_.configure(config_, millis());

#if ESP_ARDUINO_VERSION_MAJOR >= 3
    esp_task_wdt_config_t wdt = {};
    wdt.timeout_ms = kWatchdogS * 1000;
    wdt.trigger_panic = true;
    esp_task_wdt_reconfigure(&wdt);
#else
    esp_task_wdt_init(kWatchdogS, true);
#endif
    xTaskCreatePinnedToCore(controlTask, "control", 12288, this, 3, &task_, 1);
}

void App::controlTask(void* self) { static_cast<App*>(self)->controlLoop(); }

// ---------- control loop ----------

void App::controlLoop() {
    esp_task_wdt_add(nullptr);
    cc::Config cfg = config();
    std::vector<bool> wasOn;

    for (;;) {
        const uint32_t start = millis();
        std::vector<Command> commands;
        bool reconfigure = false;
        {
            Lock lock(mutex_);
            commands.swap(pending_);
            if (configDirty_) {
                cfg = config_;
                configDirty_ = false;
                reconfigure = true;
            }
        }
        if (reconfigure) {
            outputs_.configure(cfg);
            sensors_.configure(cfg);
            fusion_.configure(cfg);
            controller_.configure(cfg, start);
            wasOn.clear();
            ESP_LOGI(TAG, "new configuration active");
        }
        for (const Command& c : commands) {
            if (c.manual) controller_.setManual(c.output, c.level, c.durationS, start);
            else controller_.setAuto(c.output);
        }

        samples_ = sensors_.poll(millis(), controller_);
        cc::applyCalibration(cfg, samples_);
        const uint32_t now = millis();
        fusion_.update(samples_, now);

        time_t epoch = time(nullptr);
        int minuteOfDay = -1;
        if (epoch > kMinValidEpoch) {
            tm local;
            localtime_r(&epoch, &local);
            minuteOfDay = local.tm_hour * 60 + local.tm_min;
        } else {
            epoch = 0;
        }

        controller_.tick(fusion_, now, minuteOfDay);
        outputs_.apply(controller_.outputs());

        const auto& states = controller_.outputs();
        wasOn.resize(states.size(), false);
        for (size_t i = 0; i < states.size(); i++) {
            if (states[i].on != wasOn[i]) {
                ESP_LOGI(TAG, "%s -> %s (%s)", states[i].name.c_str(), states[i].on ? "ON" : "off", states[i].reason);
                wasOn[i] = states[i].on;
            }
        }

        cc::Snapshot snap = cc::makeSnapshot(cfg, samples_, fusion_, controller_, now, epoch);
        snap.simulated = sensors_.simulated();
        {
            Lock lock(mutex_);
            snapshot_ = std::move(snap);
        }

        esp_task_wdt_reset();
        const uint32_t elapsed = millis() - start;
        // Sleep until the next tick, or until a command wakes us up.
        if (elapsed < kTickMs) ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(kTickMs - elapsed));
    }
}

// ---------- thread-safe API for the network side ----------

cc::Config App::config() {
    Lock lock(mutex_);
    return config_;
}

uint32_t App::configVersion() {
    Lock lock(mutex_);
    return configVersion_;
}

cc::Snapshot App::snapshot() {
    Lock lock(mutex_);
    return snapshot_;
}

std::vector<cc::WebUser> App::users() {
    Lock lock(mutex_);
    return config_.web.users;
}

static bool needsReboot(const cc::Config& a, const cc::Config& b) {
    return a.deviceName != b.deviceName || a.wifi.apEnabled != b.wifi.apEnabled || a.wifi.apSsid != b.wifi.apSsid ||
           a.wifi.apPassword != b.wifi.apPassword || a.wifi.staSsid != b.wifi.staSsid ||
           a.wifi.staPassword != b.wifi.staPassword || a.mqtt.enabled != b.mqtt.enabled || a.mqtt.host != b.mqtt.host ||
           a.mqtt.port != b.mqtt.port || a.mqtt.user != b.mqtt.user || a.mqtt.password != b.mqtt.password ||
           a.mqtt.baseTopic != b.mqtt.baseTopic || a.time.tz != b.time.tz || a.time.ntpServer != b.time.ntpServer;
}

bool App::updateConfig(const std::string& json, const cc::Actor& who, cc::Role role, uint32_t expectedVersion,
                       std::vector<std::string>& errors, std::vector<std::string>& changes, bool& rebootRequired) {
    cc::Config current;
    uint32_t version;
    {
        Lock lock(mutex_);
        current = config_;
        version = configVersion_;
    }
    if (expectedVersion != 0 && expectedVersion != version) {
        errors.push_back("the configuration was changed in the meantime (by someone else?). Reload and try again.");
        return false;
    }
    cc::Config next = current;
    if (!cc::parseConfig(json, next, errors)) return false;
    errors = cc::validateConfig(next, kChip);
    if (!errors.empty()) return false;
    if (role != cc::Role::Admin && !cc::onlySetpointsChanged(current, next)) {
        errors.push_back("operators may only change setpoints (thresholds, schedules, cycle times, alarm limits)");
        return false;
    }
    changes = cc::describeChanges(current, next);
    if (changes.empty()) return true;  // nothing to store or log
    if (!saveConfig(next)) {
        errors.push_back("could not write the config to flash");
        return false;
    }
    rebootRequired = needsReboot(current, next);
    {
        Lock lock(mutex_);
        config_ = next;
        configDirty_ = true;
        configVersion_++;
        configSource_ = "flash";
    }
    xTaskNotifyGive(task_);
    std::string detail;
    for (const std::string& c : changes) detail += (detail.empty() ? "" : "\n") + c;
    logEvent(who, "config", detail);
    return true;
}

bool App::setOutput(const std::string& name, const std::string& mode, float level, uint32_t minutes,
                    const cc::Actor& who, std::string& error) {
    Command c{name, true, 0, minutes * 60};
    std::string what;
    if (mode == "auto") {
        c.manual = false;
        what = "automatic";
    } else if (mode == "on") {
        c.level = 1;
        what = "on";
    } else if (mode == "off") {
        c.level = 0;
        what = "off";
    } else if (mode == "level") {
        if (!(level >= 0 && level <= 1)) {
            error = "level must be between 0 and 1";
            return false;
        }
        c.level = level;
        what = std::to_string(static_cast<int>(level * 100 + 0.5f)) + " %";
    } else {
        error = "mode must be auto, on, off or level";
        return false;
    }
    {
        Lock lock(mutex_);
        bool known = false;
        for (const cc::OutputConfig& o : config_.outputs) known |= o.name == name;
        if (!known) {
            error = "unknown output '" + name + "'";
            return false;
        }
        pending_.push_back(c);
    }
    xTaskNotifyGive(task_);
    if (c.manual) what += minutes ? " for " + std::to_string(minutes) + " min" : " (default override time)";
    logEvent(who, "output", name + ": " + what);
    return true;
}

void App::setClock(int64_t epoch, const cc::Actor& who) {
    if (epoch <= kMinValidEpoch) return;
    const int64_t before = nowEpoch();
    timeval tv = {static_cast<time_t>(epoch), 0};
    settimeofday(&tv, nullptr);
    logEvent(who, "clock", before ? "clock corrected by " + std::to_string(epoch - before) + " s" : "clock set");
}

void App::requestReboot(const cc::Actor& who) {
    logEvent(who, "reboot", "reboot requested");
    rebootAtMs_ = millis() + 1000;
}

bool App::clockValid() { return time(nullptr) > kMinValidEpoch; }
