#include "StatusJson.h"

#include <Arduino.h>
#include <esp_system.h>

#include "App.h"
#include "Mqtt.h"
#include "Network.h"

static const char* resetReasonName() {
    switch (esp_reset_reason()) {
        case ESP_RST_POWERON: return "power-on";
        case ESP_RST_SW: return "software restart";
        case ESP_RST_PANIC: return "crash";
        case ESP_RST_INT_WDT:
        case ESP_RST_TASK_WDT:
        case ESP_RST_WDT: return "watchdog";
        case ESP_RST_BROWNOUT: return "brownout (supply voltage dropped)";
        case ESP_RST_DEEPSLEEP: return "deep sleep";
        case ESP_RST_EXT: return "reset pin";
        default: return "unknown";
    }
}

std::string statusJson(const cc::Snapshot& snapshot) {
    JsonDocument doc;
    JsonObject root = doc.to<JsonObject>();
    cc::snapshotToJson(snapshot, root);

    JsonObject sys = root["system"].to<JsonObject>();
    sys["device"] = app.config().deviceName;
    sys["firmware"] = FIRMWARE_VERSION;
    sys["chip"] = ESP.getChipModel();
    sys["resetReason"] = resetReasonName();
    sys["freeHeap"] = ESP.getFreeHeap();
    sys["minFreeHeap"] = ESP.getMinFreeHeap();
    sys["configSource"] = app.configSource();
    sys["clockSet"] = app.clockValid();
    sys["mqttConnected"] = mqtt.connected();
    network.toJson(sys["wifi"].to<JsonObject>());

    std::string out;
    serializeJson(doc, out);
    return out;
}
