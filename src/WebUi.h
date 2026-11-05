#pragma once
// Web interface on port 80, reachable through the device's own access point (http://192.168.4.1)
// and on the farm network (http://<device name>.local).
//
//   GET  /                 the UI (single page, see WebPage.h)
//   GET  /api/status       live data
//   GET  /api/config       configuration (passwords masked)
//   POST /api/config       JSON body, merged into the config        [login]
//   POST /api/output       name, mode=auto|on|off|level, level, minutes [login]
//   POST /api/time         epoch (seconds), sets the clock if there is no NTP [login]
//   POST /api/reboot                                                [login]
//   POST /update           firmware .bin upload (over-the-air update)  [login]
//
// Reading is open to everyone on the network; changing anything needs the web user/password.

#include <WebServer.h>

#include <string>
#include <vector>

class WebUi {
   public:
    void begin();
    void loop() { server_.handleClient(); }

   private:
    bool authorized();
    void sendJson(int code, const std::string& json);
    void sendResult(bool ok, const std::vector<std::string>& messages, bool rebootRequired = false);

    WebServer server_{80};
    bool uploadAuthorized_ = false;
};

extern WebUi web;
