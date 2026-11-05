#pragma once
// Web interface on port 80, reachable through the device's own access point (http://192.168.4.1)
// and on the farm network (http://<device name>.local).
//
//   GET  /                 the UI (single page, see WebPage.h)
//   GET  /api/status       live data                                          open
//   GET  /api/config       configuration (passwords masked);                  open
//                          response header X-Config-Version
//   GET  /api/me           {"user": ..., "role": ...}                          [login]
//   GET  /api/events       change log, newest first                           [login]
//   POST /api/config       JSON body, merged into the config;                 [login; operators: setpoints only]
//                          ?version=N refuses the change if the config changed meanwhile
//   POST /api/output       name, mode=auto|on|off|level, level, minutes       [login]
//   POST /api/time         epoch (seconds), sets the clock if there is no NTP [login]
//   POST /api/reboot                                                          [admin]
//   POST /update           firmware .bin upload (over-the-air update)          [admin]
//
// Reading the live data is open to everyone on the network; everything else needs a login from
// web.users (HTTP Basic auth). Every change is written to the change log with the user's name.

#include <WebServer.h>

#include <string>
#include <vector>

#include "Config.h"
#include "Events.h"

class WebUi {
   public:
    void begin();
    void loop() { server_.handleClient(); }

   private:
    // Checks the login. On success fills who/role and returns true; otherwise answers 401/403.
    bool authorize(bool adminOnly, cc::Actor& who, cc::Role& role);
    bool checkLogin(cc::Actor& who, cc::Role& role);
    void logFailedLogin();
    void sendJson(int code, const std::string& json);
    void sendResult(bool ok, const std::vector<std::string>& messages, bool rebootRequired = false);

    WebServer server_{80};
    bool uploadAuthorized_ = false;
    cc::Actor uploadBy_;
    std::string uploadName_;
    size_t uploadSize_ = 0;
    uint32_t lastFailedLogMs_ = 0;
    uint32_t suppressedFailures_ = 0;
};

extern WebUi web;
