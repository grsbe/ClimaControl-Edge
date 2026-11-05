#include "WebUi.h"

#include <ArduinoJson.h>
#include <Update.h>

#include "App.h"
#include "StatusJson.h"
#include "WebPage.h"

static const char* TAG = "web";

WebUi web;

bool WebUi::authorized() {
    std::string user, password;
    app.credentials(user, password);
    if (server_.authenticate(user.c_str(), password.c_str())) return true;
    server_.requestAuthentication(BASIC_AUTH, "ClimaControl");
    return false;
}

void WebUi::sendJson(int code, const std::string& json) {
    server_.sendHeader("Cache-Control", "no-store");
    server_.send(code, "application/json", json.c_str());
}

void WebUi::sendResult(bool ok, const std::vector<std::string>& messages, bool rebootRequired) {
    JsonDocument doc;
    doc["ok"] = ok;
    JsonArray arr = doc[ok ? "notes" : "errors"].to<JsonArray>();
    for (const std::string& m : messages) arr.add(m);
    if (rebootRequired) doc["rebootRequired"] = true;
    std::string out;
    serializeJson(doc, out);
    sendJson(ok ? 200 : 400, out);
}

void WebUi::begin() {
    server_.on("/", HTTP_GET, [this]() { server_.send_P(200, "text/html", kWebPage); });

    server_.on("/api/status", HTTP_GET, [this]() { sendJson(200, statusJson(app.snapshot())); });

    server_.on("/api/config", HTTP_GET, [this]() { sendJson(200, cc::configToJson(app.config(), false)); });

    server_.on("/api/config", HTTP_POST, [this]() {
        if (!authorized()) return;
        std::vector<std::string> errors;
        bool reboot = false;
        if (app.updateConfig(server_.arg("plain").c_str(), errors, reboot)) sendResult(true, {}, reboot);
        else sendResult(false, errors);
    });

    server_.on("/api/output", HTTP_POST, [this]() {
        if (!authorized()) return;
        std::string error;
        const bool ok = app.setOutput(server_.arg("name").c_str(), server_.arg("mode").c_str(),
                                      server_.arg("level").toFloat(), server_.arg("minutes").toInt(), error);
        sendResult(ok, ok ? std::vector<std::string>{} : std::vector<std::string>{error});
    });

    server_.on("/api/time", HTTP_POST, [this]() {
        if (!authorized()) return;
        app.setClock(atoll(server_.arg("epoch").c_str()));
        sendResult(app.clockValid(), app.clockValid() ? std::vector<std::string>{}
                                                      : std::vector<std::string>{"invalid epoch"});
    });

    server_.on("/api/reboot", HTTP_POST, [this]() {
        if (!authorized()) return;
        sendResult(true, {});
        app.requestReboot();
    });

    // Over-the-air update: the second handler receives the file in chunks, the first one answers.
    server_.on(
        "/update", HTTP_POST,
        [this]() {
            if (!authorized()) return;
            const bool ok = uploadAuthorized_ && !Update.hasError();
            sendResult(ok, ok ? std::vector<std::string>{"update installed, rebooting"}
                              : std::vector<std::string>{std::string("update failed: ") + Update.errorString()});
            if (ok) app.requestReboot();
        },
        [this]() {
            HTTPUpload& up = server_.upload();
            if (up.status == UPLOAD_FILE_START) {
                std::string user, password;
                app.credentials(user, password);
                uploadAuthorized_ = server_.authenticate(user.c_str(), password.c_str());
                if (!uploadAuthorized_) return;
                ESP_LOGI(TAG, "firmware upload %s", up.filename.c_str());
                Update.begin(UPDATE_SIZE_UNKNOWN);
            } else if (!uploadAuthorized_) {
                return;
            } else if (up.status == UPLOAD_FILE_WRITE) {
                Update.write(up.buf, up.currentSize);
            } else if (up.status == UPLOAD_FILE_END) {
                Update.end(true);
            } else if (up.status == UPLOAD_FILE_ABORTED) {
                Update.abort();
            }
        });

    server_.onNotFound([this]() { server_.send(404, "text/plain", "not found"); });
    server_.begin();
}
