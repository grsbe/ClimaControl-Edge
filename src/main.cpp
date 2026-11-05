// ClimaControl firmware for the Wildnismanufaktur mushroom farm.
//
// Two FreeRTOS tasks:
//   control  (App.cpp)   sensors -> controller -> outputs, once per second, independent of WiFi
//   loop     (this file) WiFi, web UI, MQTT
// Both are guarded by the task watchdog: if either hangs for 15 s the ESP32 reboots.

#include <Arduino.h>
#include <esp_task_wdt.h>

#include "App.h"
#include "Mqtt.h"
#include "Network.h"
#include "WebUi.h"

void setup() {
    Serial.begin(115200);
    ESP_LOGI("main", "ClimaControl " FIRMWARE_VERSION);
    app.begin();  // first: puts the outputs into their safe state and starts the control task

    const cc::Config cfg = app.config();
    network.begin(cfg);
    web.begin();
    mqtt.begin(cfg);
    esp_task_wdt_add(nullptr);
}

void loop() {
    network.loop();
    web.loop();
    mqtt.loop();
    esp_task_wdt_reset();
    if (app.rebootDue()) ESP.restart();
    delay(2);
}
