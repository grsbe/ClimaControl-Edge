# Part 1 – Overview and big picture

This is part 1 of the ClimaControl documentation.

| Part | For whom | Content |
|------|----------|---------|
| **1 – Overview** (this file) | everyone | what the system does, all hardware and software parts, how they fit together |
| [2 – Building blocks](02-building-blocks.md) | people changing or reviewing the code | every module in detail: purpose, interface, behaviour, configuration, failure handling |
| [3 – Beginner guide](03-beginner-guide.md) | people who have never programmed a microcontroller | microcontroller basics, tools, and a guided tour through this code |

---

## 1. What the system is for

The Wildnismanufaktur grows mushrooms in a breeding chamber. Mushrooms need a narrow climate: high
humidity, controlled CO₂ (they breathe it out, and too much ruins the fruiting bodies), the right
temperature and a light rhythm. If the climate is wrong for a few hours, a whole batch can be lost.

**ClimaControl** is a small control box that:

1. **measures** the climate in the chamber, in the incoming air and in the surrounding room;
2. **switches** the climate devices (fans, humidifier, light, UV) automatically according to rules;
3. lets people on site **watch and control** it with a phone, through its own WiFi network and a web page;
4. reports to a **server** over MQTT, which can record history and also control everything remotely;
5. keeps working safely when parts fail (sensor broken, WiFi gone, software hang).

The software in this repository is the **firmware**, the program that runs on the ESP32 microcontroller
inside the box.

---

## 2. The hardware

The block diagram (`hardware.jpg`) shows the board:

```
                         230 V / 1.5 kW mains
                               │
               ┌───────────────┼──────────────────────────────┐
               │        Power supply 5 V / 2 A                │
               │               │                              │
 SENSORS       │        PoL regulator 5 V → 3.3 V      Grid sync (zero-cross, not used yet)
               │               │                              │
 Breeding chamber              ▼                              │           ACTUATORS (SSR outputs)
  SCD41 + BME680 ─ I²C ext ─┐  ┌──────────────────────┐       │
  SCD41 + BME680 ─ I²C ext ─┤  │        ESP32         │ OUT1 ─ SSR 8 A ─ inflow HEPA fan   600 W
 Inflow  BME280  ─ I²C ext ─┼─ │ ┌──────────────────┐ │ OUT2 ─ SSR 8 A ─ breeding humidifier 400 W
 Ambient BME280  ─ I²C ext ─┤  │ │ WiFi: web UI     │ │ OUT3 ─ SSR 8 A ─ breeding UV       ≤250 W
 2× spare I²C    ─ I²C ext ─┘  │ │       MQTT client│ │ OUT4 ─ SSR 8 A ─ breeding light    ≤250 W
        │                      │ └──────────────────┘ │ OUT5 ─ SSR 2 A ─ inlet air fans    3×75 W
   TCA9548 I²C multiplexer ────│                      │ OUT6 ─ SSR 2 A ─ outlet air fan      75 W
                               │                      │ OUT7/8 SSR 2 A ─ 2× PWM, e.g. fan speed
 CAN interface (not used yet) ─│                      │
 Key + LCD (optional, not yet) ┘──────────────────────┘
```

### 2.1 Parts list

| Part | What it is | Role here |
|------|------------|-----------|
| **ESP32 / ESP32-S3** | A cheap microcontroller (a small computer on one chip) with built-in WiFi | Runs this firmware. Two board types are supported, see [2.2](#22-supported-boards). |
| **SCD41** (×2) | Sensirion CO₂ sensor, also measures temperature and humidity | Chamber CO₂, the most important value for ventilation |
| **BME680** (×2) | Bosch sensor: temperature, humidity, pressure, gas resistance ("VOC") | Second, independent source for chamber temperature and humidity |
| **BME280** (×2) | Bosch sensor: temperature, humidity, pressure | Inflow air and ambient room air |
| **TCA9548** | I²C multiplexer: one bus in, 8 switchable buses out | Several sensors with the same address can share the ESP32, and one broken sensor cannot block the others |
| **I²C extenders** | Bus buffers that allow longer cables | Sensors can sit metres away from the board |
| **SSR** (×8) | Solid-state relays: a small 3.3 V signal switches 230 V | Switch the mains devices. OUT7/8 are meant for PWM (speed control) |
| **Power supply + regulator** | 230 V → 5 V → 3.3 V | Powers the board |
| **Grid sync, CAN, LCD + keys** | Zero-cross detector, field bus, local display | On the board; **not implemented in the firmware yet** |

### 2.2 Supported boards

The firmware is built and checked for exactly these two board types. Other ESP32 boards may work but
are not supported.

| | **ESP32-S3-DevKitC-1** (and clones) | **ESP32-DevKitC-32E** (classic ESP32) |
|---|---|---|
| Chip | ESP32-S3, 2 cores, 240 MHz | ESP32, 2 cores, 240 MHz |
| Module variants | N8R8 (8 MB flash, 8 MB PSRAM), also N16R8 etc. | ESP32-WROOM-32E (4 MB flash), also the -32UE with external antenna |
| Build environment | `esp32s3` (default), `esp32s3_sim`, `wokwi` | `esp32dev`, `esp32dev_sim` |
| Default I²C pins | SDA 8, SCL 9 | SDA 21, SCL 22 |
| Default output pins OUT1–8 | GPIO 4, 5, 6, 7, 15, 16, 17, 18 | GPIO 16, 17, 18, 19, 23, 25, 26, 27 |
| USB | two USB-C ports: flash and monitor through the one labelled **UART**/**COM** | one USB port |
| Status | **default board.** Runs on real hardware with simulated sensors | compiles; **not yet tested on real hardware** |

The firmware detects the chip when it is built, and uses the matching pin rules
([Part 2, section 4](02-building-blocks.md#4-pins)) and default pins. The application uses the first
4 MB of flash on both, so larger modules work unchanged.

### 2.3 Hardware details that live in the configuration

- **Pin assignment.** The factory configuration uses placeholder pins (S3: OUT1–8 = GPIO 4, 5, 6, 7, 15,
  16, 17, 18; I²C SDA 8, SCL 9). All pins are configuration values, so each board's real wiring is
  entered without changing code.
- **Setpoints per mushroom species.** The factory values (e.g. humidity 85–90 %, CO₂ 800–1000 ppm) are
  placeholders and are also only configuration.
- **PWM over SSR.** Standard zero-cross SSRs cannot switch at 1 kHz. How OUT7/8 control fan speed
  (DC fans with a PWM input, or phase control via the grid-sync input) depends on the board. The PWM
  frequency is configurable per output.
- **Pull-down resistors on the SSR inputs** are required (see section 6).

---

## 3. The software at a glance

The firmware is written in **C++** with the **Arduino framework** for ESP32 and built with
**PlatformIO**. It is split into two layers:

```
┌────────────────────────────────────────────────────────────────────────────────────┐
│ src/   ESP32-specific code (needs the real chip or a simulator)                    │
│                                                                                    │
│   main.cpp   ─ starts everything                                                   │
│   App        ─ owns the configuration, runs the CONTROL TASK once per second       │
│   Sensors    ─ talks I²C: multiplexer, SCD41, BME680, BME280, bus recovery         │
│   Outputs    ─ drives the SSR pins (on/off and PWM)                                │
│   Network    ─ WiFi access point + farm WiFi, clock (NTP), mDNS name               │
│   WebUi      ─ web server: page (WebPage.h) + HTTP API + firmware update           │
│   Mqtt       ─ connection to the server: publish data, receive commands            │
│   StatusJson ─ turns the status into JSON (with system info)                       │
├────────────────────────────────────────────────────────────────────────────────────┤
│ lib/core/   pure logic, no hardware access → compiled and unit-tested on a PC      │
│                                                                                    │
│   Config      ─ all settings: defaults, JSON read/write, validation                │
│   Pins        ─ which GPIOs are safe for what                                      │
│   Types       ─ quantities, sensor types, value ranges                             │
│   Fusion      ─ merges redundant sensors into one value per "signal"               │
│   Controller  ─ rules → output levels, failsafe, timing limits, manual, alarms     │
│   Sensirion   ─ SCD41 protocol (checksums, unit conversion)                        │
│   Status      ─ snapshot of everything for UI and MQTT                             │
│   Simulation  ─ a simple grow-room model for testing without sensors               │
└────────────────────────────────────────────────────────────────────────────────────┘
```

**Why two layers?** The decisions that keep the mushrooms alive (when the humidifier runs, what
happens when a sensor dies) live in `lib/core` and do not touch hardware. That makes them testable on
any PC in seconds. The automated tests include a simulated 4-hour run of the default configuration. The `src/` layer is thin and only moves data between the chips and the core.

---

## 4. How it works, step by step

### 4.1 Two tasks running side by side

The ESP32 runs **FreeRTOS**, a small operating system that lets several "tasks" run at the same
time. The firmware uses two:

```
   CONTROL TASK  (priority 3, CPU core 1)            NETWORK LOOP  (Arduino loop(), priority 1)
   ───────────────────────────────────────           ────────────────────────────────────────
   every 1 s:                                        continuously:
     1. take queued commands / new config   ◄─────     web requests  ─┐
     2. read sensors (I²C)                             MQTT messages ─┼─► App: setOutput(), updateConfig()
     3. fuse readings into signals                                    │       (queued, mutex-protected)
     4. controller decides output levels               WiFi reconnect │
     5. write the SSR pins                                            │
     6. store a snapshot ─────────────────────────►    status page / MQTT publish read app.snapshot()
     7. feed the watchdog                              feed the watchdog
```

The control task **never waits for the network**. If WiFi drops, the web server hangs, or the MQTT
server is unreachable, the climate control continues unchanged. The two sides only exchange data
through a lock-protected copy (the *snapshot*) and a command queue.

### 4.2 One control cycle (every second)

```
 sensors ──► raw samples ──► calibration ──► FUSION ──► signals ──► CONTROLLER ──► levels ──► OUTPUTS ──► SSRs
 (6 devices)  per sensor     (+offset)       median,     e.g.        rules,          0..1       GPIO / PWM
                                              stale &     chamber.co2 failsafe,
                                              range check = 870 ppm   min/max times,
                                                                      manual override
                                                                      ──► alarms
```

1. **Sensors** read whatever is due (SCD41 delivers a new value every 5 s, BME280 is read every 2 s,
   BME680 every 3 s).
2. **Calibration** adds a configurable offset per sensor and quantity.
3. **Fusion** groups readings into **signals** named `group.quantity`, for example `chamber.co2`,
   `chamber.humidity` or `ambient.temperature`. The value is the **median** of all working sensors.
   Readings that are older than 30 s or physically impossible are dropped.
4. **Controller** evaluates the **rules** of each output (see 4.3) and applies the safety timing.
5. **Outputs** write the result to the pins.
6. A **snapshot** of everything is stored for the web page and MQTT.

### 4.3 Rules

Each output has a list of rules. **The output takes the highest level of all its rules**: it is on if
any rule says on.

| Rule | Example | Meaning |
|------|---------|---------|
| `above` | CO₂ on at ≥ 1000, off at ≤ 800 | for ventilation (hysteresis prevents flicker) |
| `below` | humidity on at ≤ 85, off at ≥ 90 | for the humidifier |
| `schedule` | 08:00–20:00 | light rhythm, may span midnight; needs a valid clock |
| `cycle` | 5 min on every 30 min | periodic fresh air regardless of sensors |
| `linear` | CO₂ 800→1500 ppm gives fan speed 30→100 % | smooth PWM speed |

### 4.4 Default setup (placeholders)

| Output | Pin (S3) | Default rule(s) | If data is missing |
|--------|----------|-----------------|--------------------|
| `hepa` – inflow HEPA | 4 | CO₂ above 1000/800, min. 60 s on | **on** |
| `humidifier` | 5 | humidity below 85/90, max. 30 min on then 5 min pause | off |
| `uv` | 6 | none (manual only) | off |
| `light` | 7 | schedule 08:00–20:00 | off |
| `inlet` – inlet air | 15 | CO₂ above 1000/800 **or** 5 min every 30 min | **on** |
| `outlet` – outlet air | 16 | CO₂ above 1000/800 **or** humidity above 97/94 **or** 5/30 min cycle | **on** |
| `fan_pwm_1` | 17 | CO₂ 800–1500 ppm → 30–100 % | **100 %** |
| `fan_pwm_2` | 18 | none | off |

Alarms by default: chamber humidity < 75 %, CO₂ > 2000 ppm, temperature outside 12–30 °C, each for
longer than 5 minutes.

---

## 5. Ways to talk to the controller

```
           phone / laptop on site                         server (later)
                  │                                             │
        WiFi "ClimaControl" (own access point)         farm WiFi / LAN
                  │                                             │
          http://192.168.4.1                       MQTT broker (e.g. Mosquitto)
                  │                                             │
                  ▼                                             ▼
            ┌───────────────────────────── ESP32 ──────────────────────────────┐
            │  Web UI + HTTP API (port 80)          MQTT client                 │
            │  Overview · Outputs · Sensors ·       publishes status, signals,  │
            │  Configuration · System               alarms, config              │
            │                                       receives cmd/output, cmd/   │
            │  also: http://climacontrol.local      config, cmd/reboot          │
            └───────────────────────────────────────────────────────────────────┘
                  │
              USB cable: flashing + serial log (115200 baud)
```

| Channel | Who uses it | What it can do | Protection |
|---------|-------------|----------------|------------|
| **Own WiFi access point** `ClimaControl` | people in the building | everything via the web page | WPA2 password; changes need web login |
| **Web UI / HTTP API** | browser, scripts | read status, switch outputs, edit config, set clock, firmware update, reboot | reading is open to anyone on the network; every change needs HTTP Basic login |
| **MQTT** | the server | the same as the web UI | broker user/password; no TLS yet, so use it on a trusted network or VPN |
| **USB serial** | developer | flashing, log output | physical access |

---

## 6. Safety and redundancy concept

The requirement is that a failure must not kill the mushrooms. The design has several layers:

| Threat | Protection in the firmware |
|--------|----------------------------|
| One sensor fails | Redundant sensors per signal (2× SCD41 for CO₂, 4 sources for chamber humidity and temperature). The median of the remaining sensors is used; a **degraded** warning appears. |
| Sensor delivers nonsense | Plausibility range per quantity, and readings older than 30 s are dropped. When sensors of the same signal differ more than a tolerance, a **disagree** warning appears. |
| All sensors of a signal fail | Every output whose rules need that signal goes to its **failsafe level**. Ventilation fails **on** (fresh air is the safest state), humidifier, UV and light fail **off**. Critical alarm. |
| Sensor or I²C bus hangs | Failed sensors are re-initialised every 5 s. If the multiplexer stops answering, the bus is reset (9 clock pulses + STOP). Every I²C transaction has a 50 ms timeout. |
| Clock unknown (no internet after a power cut) | Schedule rules go to failsafe; the clock can be set from the web UI. |
| Humidifier (or anything) stuck on | `maxOnS`: forced off after a maximum on-time, then a cooldown pause. |
| Devices switching too often | `minOnS` / `minOffS` per output. |
| Someone forgets a manual override | Manual on/off expires after 1 h by default and returns to automatic. |
| Network problems | The control task is independent of WiFi, web and MQTT. |
| Software hangs | **Watchdog**: if the control task or the network loop does not report for 15 s, the ESP32 restarts. After restart it continues with the stored configuration. |
| Bad configuration | Every change is validated before it is stored (pin safety, thresholds, signal names, …). It is written atomically (temp file + rename), and the previous version is kept as a backup and used if the main file is damaged. |
| Dangerous pins | Outputs refuse ESP32 pins that toggle during boot (strapping pins), flash pins and USB pins. |
| Controller goes offline | MQTT "last will": the broker immediately publishes `offline` to the server. |

**What the hardware must add**, because firmware alone cannot do it:

- **Pull-down resistors on every SSR input.** During reset and boot (about 0.5 s, also after every
  firmware update) the ESP32 pins float. Without pull-downs the SSRs can switch randomly.
- **SSRs usually fail shorted (stuck on).** An independent backstop for the dangerous loads is worth it:
  for example a mechanical hygrostat in series with the humidifier.
- A **battery-backed real-time clock** (e.g. DS3231 on a spare I²C port), so the light schedule works
  after a power cut without internet. Not supported by the firmware yet.
- A small **UPS** for the controller and router, and a **spare board** flashed and ready.
- The **server should alert a person** on `availability = offline` and on critical alarms.

---

## 7. Startup sequence

```
power on / reset
  │  (pins float ~0.5 s → pull-downs keep the SSRs off)
  ▼
setup()
  ├─ Serial at 115200 baud
  ├─ app.begin()
  │    ├─ mount flash file system (LittleFS)
  │    ├─ load /config.json → else /config.bak.json → else factory defaults
  │    ├─ outputs → their FAILSAFE level   (first thing that touches the pins)
  │    ├─ set time zone, configure sensors, fusion, controller
  │    ├─ watchdog 15 s
  │    └─ start the CONTROL TASK (runs from now on, every second)
  ├─ network.begin()   access point + farm WiFi + NTP + mDNS
  ├─ web.begin()       web server on port 80
  ├─ mqtt.begin()      if enabled
  └─ register the network loop with the watchdog
  ▼
loop() forever: network → web → MQTT → feed watchdog → reboot if requested
```

---

## 8. Repository layout

```
wildnismanufaktur-edge/
├── platformio.ini        build configuration: boards, libraries, environments
├── lib/core/src/         hardware-independent logic (Part 2, sections 3–10)
├── src/                  ESP32 code (Part 2, sections 11–17)
├── test/test_core/       unit tests for lib/core
├── server/               server stack prototype: MQTT broker, Telegraf, InfluxDB, Grafana (Docker Compose)
├── tools/                Dockerfile + pio.ps1: build and test with only Docker installed
├── wokwi.toml            Wokwi simulator settings
├── diagram.json          Wokwi virtual wiring: ESP32-S3 + 8 LEDs as outputs
├── docs/                 this documentation
├── hardware.jpg          block diagram
├── todo.md               original task description
└── README.md             short quick-start
```

### Build environments

| Environment | Board | Sensors | Use |
|-------------|-------|---------|-----|
| `esp32s3` (default) | ESP32-S3-DevKitC-1 and clones | real | the production controller |
| `esp32s3_sim` | ESP32-S3 | **simulated** | try a bare board on the desk |
| `wokwi` | ESP32-S3 in the Wokwi simulator | simulated | no board needed; joins `Wokwi-GUEST` instead of opening an access point |
| `esp32dev` | classic ESP32 DevKitC-32E | real | alternative board |
| `esp32dev_sim` | classic ESP32 | simulated | |
| `native` | your PC | – | unit tests |

---

## 9. Implementation status

**Keep this table up to date.** Anything marked *not yet* is a known gap: do not rely on it in
production.

| Area | Status |
|------|--------|
| Control logic (rules, failsafe, timing limits, manual override, alarms) | implemented, covered by unit tests |
| Sensors: SCD41, BME680, BME280 behind a TCA9548 multiplexer | implemented; **not yet tested with real sensors** |
| Relay and PWM outputs | implemented; **not yet tested with real SSRs** |
| Web UI, HTTP API, over-the-air update | implemented, runs on a real ESP32-S3 with simulated sensors |
| Personal logins (admin / operator), Setpoints form, change log | implemented, tested on a real ESP32-S3 |
| Classic ESP32 (DevKitC-32E) | builds; **not yet tested on real hardware** |
| MQTT client (status, alarms, remote control) | implemented; **not yet tested against a broker** |
| Wokwi simulator setup | configured; **not yet tried** |
| Pin assignment and setpoints | **not yet decided**; the factory values are placeholders in the configuration |
| Grid sync (zero-cross) | **not implemented yet** |
| CAN interface | **not implemented yet** |
| LCD + keys | **not implemented yet** |
| Battery-backed real-time clock | **not implemented yet**; the clock comes from NTP or the web UI |
| BME680 air-quality index (needs Bosch's BSEC library) | **not implemented yet**; the raw gas resistance is reported |
| MQTT over TLS | **not implemented yet**; use a trusted network or VPN |
| Server: storing history, dashboards | prototype in `server/` (Telegraf + InfluxDB + Grafana), runs on a PC; **not yet deployed on a permanent server** |
| Server: push alerts via ntfy (controller offline, critical alarm, warnings) | implemented; "offline" alert tested end to end; **critical-alarm alert not yet tested with a real alarm** |
| Server: alert when the server itself is down (dead man's switch) | **not implemented yet** |
| Remote access from outside the farm network | **not set up yet**; planned via a VPN (e.g. Tailscale), giving access to Grafana and the controller page without opening anything to the internet. A separate control page on the server is not needed: the controller's own page works from every device on the farm network. |

### Glossary (short)

A longer glossary is in [Part 3](03-beginner-guide.md#glossary).

| Term | Meaning |
|------|---------|
| **Signal** | A fused value such as `chamber.co2`: the median of all working sensors of the group `chamber` that measure CO₂ |
| **Rule** | Condition that decides when an output is on |
| **Failsafe level** | The level an output takes when a rule's input is missing |
| **Snapshot** | Copy of all current values that the web page and MQTT read |
| **SSR** | Solid-state relay that switches 230 V from a 3.3 V signal |
| **Watchdog** | Hardware timer that restarts the chip if the software stops responding |
