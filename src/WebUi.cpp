#include "WebUi.h"

#include <ArduinoJson.h>
#include <Update.h>
#include <mbedtls/base64.h>

#include "App.h"
#include "StatusJson.h"
#include "WebPage.h"

static const char* TAG = "web";
static const uint32_t kFailedLoginLogIntervalMs = 30000;  // at most one log entry per 30 s

WebUi web;

// ---------- login ----------

bool WebUi::checkLogin(cc::Actor& who, cc::Role& role) {
    for (const cc::WebUser& u : app.users()) {
        if (!u.password.empty() && server_.authenticate(u.name.c_str(), u.password.c_str())) {
            who = {"web", u.name, server_.client().remoteIP().toString().c_str()};
            role = u.role;
            return true;
        }
    }
    return false;
}

// Logs a wrong user name or password (not requests that simply carry no login yet). Throttled, so
// that someone guessing passwords cannot fill the change log or wear out the flash.
void WebUi::logFailedLogin() {
    if (!server_.hasHeader("Authorization")) return;
    const uint32_t now = millis();
    if (lastFailedLogMs_ != 0 && now - lastFailedLogMs_ < kFailedLoginLogIntervalMs) {
        suppressedFailures_++;
        return;
    }
    // "Basic base64(user:password)" -> user name only, the password is never logged
    std::string user = "?";
    String header = server_.header("Authorization");
    if (header.startsWith("Basic ")) {
        unsigned char buf[128];
        size_t len = 0;
        const String b64 = header.substring(6);
        if (mbedtls_base64_decode(buf, sizeof buf - 1, &len, reinterpret_cast<const unsigned char*>(b64.c_str()),
                                  b64.length()) == 0) {
            std::string decoded(reinterpret_cast<char*>(buf), len);
            user = decoded.substr(0, decoded.find(':'));
        }
    }
    std::string detail = "wrong user name or password for '" + user + "'";
    if (suppressedFailures_) detail += " (+" + std::to_string(suppressedFailures_) + " more since the last entry)";
    app.logEvent({"web", "", server_.client().remoteIP().toString().c_str()}, "login-failed", detail);
    lastFailedLogMs_ = now;
    suppressedFailures_ = 0;
}

bool WebUi::authorize(bool adminOnly, cc::Actor& who, cc::Role& role) {
    if (!checkLogin(who, role)) {
        logFailedLogin();
        server_.requestAuthentication(BASIC_AUTH, "ClimaControl");
        return false;
    }
    if (adminOnly && role != cc::Role::Admin) {
        sendResult(false, {"this needs an admin login"});
        return false;
    }
    return true;
}

// ---------- responses ----------

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

// ---------- routes ----------

void WebUi::begin() {
    static const char* kHeaders[] = {"Authorization"};
    server_.collectHeaders(kHeaders, 1);

    server_.on("/", HTTP_GET, [this]() { server_.send_P(200, "text/html", kWebPage); });

    server_.on("/api/status", HTTP_GET, [this]() { sendJson(200, statusJson(app.snapshot())); });

    server_.on("/api/config", HTTP_GET, [this]() {
        server_.sendHeader("X-Config-Version", String(app.configVersion()));
        sendJson(200, cc::configToJson(app.config(), false));
    });

    server_.on("/api/me", HTTP_GET, [this]() {
        cc::Actor who;
        cc::Role role;
        if (!authorize(false, who, role)) return;
        JsonDocument doc;
        doc["user"] = who.user;
        doc["role"] = cc::roleName(role);
        std::string out;
        serializeJson(doc, out);
        sendJson(200, out);
    });

    server_.on("/api/events", HTTP_GET, [this]() {
        cc::Actor who;
        cc::Role role;
        if (!authorize(false, who, role)) return;
        const std::vector<cc::Event> events = app.events();
        std::string out = "[";
        for (auto it = events.rbegin(); it != events.rend(); ++it) {  // newest first
            if (out.size() > 1) out += ",";
            out += cc::eventToJson(*it);
        }
        out += "]";
        sendJson(200, out);
    });

    server_.on("/api/config", HTTP_POST, [this]() {
        cc::Actor who;
        cc::Role role;
        if (!authorize(false, who, role)) return;
        std::vector<std::string> errors, changes;
        bool reboot = false;
        const uint32_t version = server_.hasArg("version") ? server_.arg("version").toInt() : 0;
        if (app.updateConfig(server_.arg("plain").c_str(), who, role, version, errors, changes, reboot)) {
            if (changes.empty()) changes.push_back("no changes");
            sendResult(true, changes, reboot);
        } else {
            sendResult(false, errors);
        }
    });

    server_.on("/api/output", HTTP_POST, [this]() {
        cc::Actor who;
        cc::Role role;
        if (!authorize(false, who, role)) return;
        std::string error;
        const bool ok = app.setOutput(server_.arg("name").c_str(), server_.arg("mode").c_str(),
                                      server_.arg("level").toFloat(), server_.arg("minutes").toInt(), who, error);
        sendResult(ok, ok ? std::vector<std::string>{} : std::vector<std::string>{error});
    });

    server_.on("/api/time", HTTP_POST, [this]() {
        cc::Actor who;
        cc::Role role;
        if (!authorize(false, who, role)) return;
        app.setClock(atoll(server_.arg("epoch").c_str()), who);
        sendResult(app.clockValid(), app.clockValid() ? std::vector<std::string>{}
                                                      : std::vector<std::string>{"invalid epoch"});
    });

    server_.on("/api/reboot", HTTP_POST, [this]() {
        cc::Actor who;
        cc::Role role;
        if (!authorize(true, who, role)) return;
        sendResult(true, {});
        app.requestReboot(who);
    });

    // Over-the-air update: the second handler receives the file in chunks, the first one answers.
    server_.on(
        "/update", HTTP_POST,
        [this]() {
            cc::Actor who;
            cc::Role role;
            if (!authorize(true, who, role)) return;
            const bool ok = uploadAuthorized_ && !Update.hasError();
            const std::string error = std::string("update failed: ") + Update.errorString();
            app.logEvent(uploadBy_, "firmware",
                         (ok ? "installed " : "upload failed: ") + uploadName_ + " (" + std::to_string(uploadSize_) +
                             " bytes)" + (ok ? "" : ", " + error));
            sendResult(ok, ok ? std::vector<std::string>{"update installed, rebooting"} : std::vector<std::string>{error});
            if (ok) app.requestReboot(uploadBy_);
        },
        [this]() {
            HTTPUpload& up = server_.upload();
            if (up.status == UPLOAD_FILE_START) {
                cc::Role role = cc::Role::Operator;
                uploadAuthorized_ = checkLogin(uploadBy_, role) && role == cc::Role::Admin;
                if (!uploadAuthorized_) return;
                uploadName_ = up.filename.c_str();
                uploadSize_ = 0;
                ESP_LOGI(TAG, "firmware upload %s", up.filename.c_str());
                Update.begin(UPDATE_SIZE_UNKNOWN);
            } else if (!uploadAuthorized_) {
                return;
            } else if (up.status == UPLOAD_FILE_WRITE) {
                Update.write(up.buf, up.currentSize);
                uploadSize_ += up.currentSize;
            } else if (up.status == UPLOAD_FILE_END) {
                Update.end(true);
            } else if (up.status == UPLOAD_FILE_ABORTED) {
                Update.abort();
            }
        });

    server_.onNotFound([this]() { server_.send(404, "text/plain", "not found"); });
    server_.begin();
}
