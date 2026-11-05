# ClimaControl firmware

Firmware for the ESP32 climate controller of the Wildnismanufaktur mushroom farm (see `hardware.jpg`).
It reads the climate sensors over I²C, switches the fans, humidifier, light and UV through the SSRs,
and offers a web interface (own WiFi access point) and MQTT for a remote server.

**Status:** compiles and passes its unit tests. It runs on a real ESP32-S3 board with simulated
sensors; real sensors and SSRs have not been tested yet. Pins and setpoints are placeholders until
the board schematic and the growing parameters are known.

## 📖 Documentation: **https://grsbe.github.io/ClimaControl-Edge/**

1. [Overview and big picture](https://grsbe.github.io/ClimaControl-Edge/01-overview.html): what the system does, all hardware and software parts
2. [Building blocks in detail](https://grsbe.github.io/ClimaControl-Edge/02-building-blocks.html): every module, plus config, HTTP and MQTT reference
3. [Beginner guide](https://grsbe.github.io/ClimaControl-Edge/03-beginner-guide.html): microcontroller basics, first programs, a guided tour of the code

The pages are the Markdown files in [`docs/`](docs/). Every push to `main` that changes them republishes
the website automatically ([.github/workflows/docs.yml](.github/workflows/docs.yml)). To preview locally,
double-click `tools\docs.cmd` (needs Docker Desktop).

## Contents

```
lib/core/       the control logic, plain C++, no hardware access -> unit-tested on the PC
  Config        configuration: defaults, JSON load/save, validation
  Fusion        merges redundant sensors (median, stale/implausible readings dropped)
  Controller    rules -> output levels, failsafe, min/max on times, manual override, alarms
  Simulation    a crude grow-room model for testing without sensors
  Sensirion     SCD41 protocol helpers
src/            ESP32-specific code
  App           config storage + the control task (runs every second, independent of WiFi)
  Sensors       TCA9548 multiplexer, SCD41, BME680, BME280, I2C bus recovery
  Outputs       relay GPIOs and PWM
  Network       access point + farm WiFi, NTP time
  WebUi         HTTP API + the page in WebPage.h
  Mqtt          MQTT client (topics documented in Mqtt.h)
test/           unit tests (pio test -e native)
server/         local MQTT broker for testing (Docker)
tools/          run PlatformIO in Docker without installing anything
```

## Getting started

### Install

1. VS Code -> Extensions -> install **PlatformIO IDE**. It downloads the compiler and ESP32 tools on first use.
2. USB driver: the dev boards use a USB-serial chip (CP210x or CH343). Windows 11 normally installs the
   driver by itself; if the board does not show up as a COM port, install the driver from the chip maker's website.

### Build and flash

Open this folder in VS Code. PlatformIO adds buttons to the bottom status bar:
✓ = build, → = upload, 🔌 = serial monitor. The environment selector is next to them:

| Environment    | Use                                                              |
|----------------|------------------------------------------------------------------|
| `esp32s3`      | the real controller (ESP32-S3-DevKitC-1 or clone, default)        |
| `esp32s3_sim`  | same board, sensors simulated: plug in a bare board and try it out |
| `wokwi`        | runs in the Wokwi simulator, no board needed (see below)          |
| `esp32dev`     | classic ESP32 (DevKitC-32E), also supported                       |
| `esp32dev_sim` | classic ESP32 with simulated sensors                              |
| `native`       | unit tests on the PC                                              |

The S3 board has two USB-C ports: flash and monitor through the one labelled **UART** (or **COM**).

### Without a board: Wokwi simulator

[Wokwi](https://wokwi.com) simulates an ESP32-S3 inside VS Code and runs the real firmware. `diagram.json`
wires 8 LEDs to the output pins, so you see the relays switch; the sensors are simulated.

1. Install the VS Code extension **Wokwi Simulator** and activate its license (F1 -> "Wokwi: Request a New License").
   Check the current license terms on wokwi.com.
2. Build the `wokwi` environment.
3. F1 -> **Wokwi: Start Simulator**, then open **http://localhost:8180**.

In Wokwi the controller joins the simulator's network instead of opening its own access point.

The first build takes a few minutes (downloads). After flashing, the serial monitor shows the log
at 115200 baud.

Without VS Code, or without installing anything except Docker:

```powershell
.\tools\pio.ps1 test -e native     # unit tests
.\tools\pio.ps1 run -e esp32s3     # compile (flashing needs USB, use the VS Code extension for that)
```

### Factory passwords (`.env`)

The factory passwords are not stored in git. Before the first build, copy `.env.example` to `.env`
(git ignores it) and choose them:

```ini
CC_DEFAULT_AP_PASSWORD=...    # own WiFi access point, 8-63 characters
CC_DEFAULT_WEB_PASSWORD=...   # login for changes in the web UI (user: admin)
```

The build stops with a clear message if `.env` is missing. The values only apply to a controller
without a stored configuration (new or erased board); after that, passwords are changed in the web UI.

### First start

1. Connect a phone or laptop to the WiFi **ClimaControl**, password `CC_DEFAULT_AP_PASSWORD` from `.env`.
2. Open **http://192.168.4.1**.
3. Changing anything asks for a login: user `admin`, password `CC_DEFAULT_WEB_PASSWORD` from `.env`.
4. **Change both passwords** under *Configuration* (`wifi.ap.password`, `web.password`).

To connect it to the farm network (needed for MQTT and automatic time), fill in `wifi.sta`, save, reboot.
It is then also reachable at `http://climacontrol.local`.

## How the control works

Every second the control task:

1. reads the sensors (each SCD41 every 5 s, BME280 every 2 s, BME680 every 3 s),
2. merges them into **signals** such as `chamber.co2` or `chamber.humidity`. A signal is the median of all
   working sensors in that group. Readings that are older than 30 s or physically impossible are ignored.
3. evaluates the **rules** of each output. An output is on if any of its rules says so.
4. applies safety timing and writes the outputs.

Rule types:

| type       | example                                                                  | meaning |
|------------|--------------------------------------------------------------------------|---------|
| `above`    | `{"type":"above","signal":"chamber.co2","on":1000,"off":800}`            | on at ≥ 1000 ppm, off again at ≤ 800 |
| `below`    | `{"type":"below","signal":"chamber.humidity","on":85,"off":90}`          | on at ≤ 85 %, off again at ≥ 90 % |
| `schedule` | `{"type":"schedule","windows":[{"from":"08:00","to":"20:00"}]}`          | on during the time windows (may span midnight) |
| `cycle`    | `{"type":"cycle","onS":300,"periodS":1800}`                              | on 5 min every 30 min |
| `linear`   | `{"type":"linear","signal":"chamber.co2","in":[800,1500],"out":[0.3,1]}` | PWM 30 % at 800 ppm rising to 100 % at 1500 ppm |

Per output also: `failsafe` (level when data is missing), `minOnS` / `minOffS` (no rapid switching),
`maxOnS` + `cooldownS` (e.g. force the humidifier off after 30 min running), `activeHigh`.

The defaults in `lib/core/src/Config.cpp` (`defaultConfig`) follow the block diagram. **All setpoints are
placeholders**: set the real values for your mushroom species in the configuration.

## Safety and redundancy

What the firmware does:

- **Control never depends on the network.** WiFi, web and MQTT run on a separate task; the control loop
  keeps running if they hang, and the watchdog reboots the ESP32 if either task freezes for 15 s.
- **Failsafe per output.** If a rule's input is missing (all sensors of a signal dead, clock not set), the
  output goes to its `failsafe` level. Defaults: fans **on** (fresh air), humidifier, UV and light **off**.
- **Sensor redundancy.** Two SCD41 + two BME680 per chamber. One failing sensor only raises a "degraded" warning;
  sensors that disagree raise a warning; failed sensors are re-initialised automatically; a hung I²C bus is reset.
- **maxOnS** limits how long e.g. the humidifier may run without a break.
- **Manual override expires** (default 1 h), so a forgotten "on" does not stay forever.
- **Config is validated** before it is stored. It is written atomically and the previous version is kept as a backup.
- **Pin check:** outputs refuse ESP32 pins that toggle during boot (strapping pins) or belong to the flash chip.
- **Alarms** (sensor faults, failsafe, limits like humidity < 75 % for 10 min) are shown in the UI and published
  via MQTT; the MQTT "last will" tells the server immediately when the controller drops off.

What the hardware should do (please pass on to whoever builds the board):

- **Pull-down resistors on every SSR input.** During reset/boot (~0.5 s, also after every firmware update)
  the ESP32 pins float; without pull-downs the SSRs can switch randomly.
- **SSRs usually fail shorted (stuck on).** A cheap independent backstop for the dangerous loads is worth
  it: a mechanical hygrostat in series with the humidifier, a thermal fuse/thermostat for heat sources.
- **Battery RTC (e.g. DS3231 on the spare I²C port)** so light schedules work after a power cut without internet.
  Until then the clock comes from NTP or the "Set clock" button in the web UI.
- A small **UPS** for the controller and the WiFi router; keep **a spare ESP32 board** flashed and ready.
- The server should **alert someone** (e-mail/push) on `availability = offline` and on critical alarms.

## MQTT

Topics are documented at the top of `src/Mqtt.h`. In short, with the default base topic `wildnis/climacontrol`:

- `.../status` full JSON every 10 s, `.../signal/chamber.co2` single values, `.../alarms`, `.../availability`
- `.../cmd/output/humidifier` with `on`, `off`, `auto`, a level like `0.5`, or `{"mode":"on","minutes":30}`
- `.../cmd/config` with a JSON snippet, e.g. `{"outputs":[...]}`; the answer arrives on `.../result`

## Server (prototype)

`server/` holds a Docker Compose stack: MQTT broker → Telegraf → InfluxDB → Grafana dashboards.

```powershell
powershell -ExecutionPolicy Bypass -File server\setup.ps1   # once: creates the secrets in server\.env
cd server; docker compose up -d                            # Grafana at http://localhost:3000 (user admin)
```

Details are in the documentation (Part 2, section 16.2).

## Not done yet

- Server: alert when the server itself is down (dead man's switch)
- Remote access from outside the farm network (planned via VPN, e.g. Tailscale)
- The server stack runs as a prototype on a PC; not yet deployed on a permanent server
- Grid sync input (zero-cross), CAN bus, LCD + keys from the block diagram
- BME680 air-quality index (needs Bosch's proprietary BSEC library; raw gas resistance is reported)
- TLS for MQTT (fine on a local network or VPN for now)
- Real pin assignment and setpoints
