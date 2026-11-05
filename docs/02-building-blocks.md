# Part 2 – Building blocks in detail

Part 2 of the ClimaControl documentation. [Part 1](01-overview.md) explains how the parts fit together,
and [Part 3](03-beginner-guide.md) explains the basics for newcomers.

Each section covers one module: **purpose**, **files**, **interface**, **behaviour**, **failure
handling** and **where it is tested**. File references are relative to the repository root.

## Contents

1. [Build system (PlatformIO)](#1-build-system-platformio)
2. [Conventions used throughout the code](#2-conventions-used-throughout-the-code)
3. [Types – quantities, sensor types, ranges](#3-types)
4. [Pins – GPIO safety rules](#4-pins)
5. [Config – settings, JSON, validation](#5-config)
6. [Fusion – merging redundant sensors](#6-fusion)
7. [Controller – rules, failsafe, timing, alarms](#7-controller)
8. [Sensirion – SCD41 protocol](#8-sensirion)
9. [Status – the snapshot](#9-status)
10. [Simulation – the room model](#10-simulation)
11. [App – config storage and the control task](#11-app)
12. [Sensors – I²C, multiplexer, drivers, bus recovery](#12-sensors)
13. [Outputs – relay GPIOs and PWM](#13-outputs)
14. [Network – WiFi, time, mDNS](#14-network)
15. [WebUi and WebPage – HTTP API and the page](#15-webui-and-webpage)
16. [Mqtt – server connection](#16-mqtt)
17. [StatusJson and main.cpp](#17-statusjson-and-maincpp)
18. [Unit tests](#18-unit-tests)
19. [Tools: Docker build, server stack, Wokwi](#19-tools)
20. [Reference: configuration keys](#20-reference-configuration-keys)
21. [Reference: status JSON, alarm codes, reason strings](#21-reference-status-json-alarm-codes-reason-strings)

---

## 1. Build system (PlatformIO)

**File:** `platformio.ini`

PlatformIO downloads the compiler, the ESP32 Arduino core and the libraries, then builds and flashes.
The file defines a shared block and one *environment* per target.

| Setting | Value | Why |
|---------|-------|-----|
| `platform` | `espressif32@^6.9.0` | Arduino-ESP32 core 2.x on ESP-IDF 4.4. The code also compiles against core 3.x (`#if ESP_ARDUINO_VERSION_MAJOR >= 3` branches). |
| `framework` | `arduino` | `setup()`/`loop()`, `Wire`, `WiFi`, `WebServer`, … |
| `board_build.partitions` | `min_spiffs.csv` | two app slots of about 1.9 MB each (needed for over-the-air updates) plus a small data partition for the config |
| `board_build.filesystem` | `littlefs` | power-cut-safe file system for `/config.json` |
| `build_flags` | `-std=gnu++17`, `-DARDUINOJSON_ENABLE_STD_STRING=1`, `-DCORE_DEBUG_LEVEL=2` | C++17; ArduinoJson accepts `std::string`; the log shows **warnings and errors** only (set `3` to also see info messages such as "hepa -> ON") |
| `monitor_speed` | 115200 | serial log baud rate |
| `monitor_filters` | `esp32_exception_decoder` | turns crash addresses into function names and line numbers |

**Libraries:** ArduinoJson 7, PubSubClient 2.8 (MQTT), Adafruit BME280, Adafruit BME680. `WebServer`,
`WiFi`, `Update`, `LittleFS` and `ESPmDNS` are part of the Arduino core.

**Compile-time switches:**

| Define | Effect |
|--------|--------|
| `CC_FORCE_SIMULATION` | sensors are always simulated, whatever the config says (`*_sim` and `wokwi` environments) |
| `CC_WOKWI` | ignores the WiFi settings: access point off, joins the open `Wokwi-GUEST` network |
| `CONFIG_IDF_TARGET_ESP32S3` | set by the toolchain; selects `Chip::Esp32S3` for pin rules and default pins |
| `CC_DEFAULT_AP_PASSWORD`, `CC_DEFAULT_WEB_PASSWORD` | factory passwords, see below |

**Factory passwords from `.env`:** the default access-point and web passwords are not in git. The
pre-build script `tools/env_defaults.py` (registered in `[env]`, so it runs for every environment,
including `native`) reads them from `.env` in the project folder (git-ignored; template:
`.env.example`), or from environment variables of the same name, which take precedence (for CI). It
passes them to the compiler as string defines that `Config.h` uses as defaults. The build fails with
an explanation if they are missing or the access-point password is not 8–63 characters, and `Config.h`
has an `#error` as a second guard. The values only matter for a controller without a stored
`/config.json`; reflashing does not change the passwords of an already configured device.

The `native` environment compiles `lib/core` and `test/` with the PC's compiler (`-Wall`) and runs the
Unity test framework.

---

## 2. Conventions used throughout the code

- **Namespace `cc`** (ClimaControl) for everything in `lib/core`.
- **No Arduino in `lib/core`.** It uses only the C++ standard library and ArduinoJson, so it builds
  on a PC.
- **Time is `uint32_t` milliseconds** from `millis()`. This wraps around after 49.7 days. Comparisons
  therefore use `cc::before(a, b)` (`int32_t(a - b) < 0`) or differences like `now - start >= duration`,
  which stay correct across the wrap. There is a test for this (`test_millis_wraparound`).
- **Levels are floats 0..1.** A relay is on at any level > 0; a PWM output uses the value as duty cycle.
- **Names** of outputs, sensors, groups and the device: 1–32 characters `a-z 0-9 - _`. They appear in
  URLs and MQTT topics.
- **Units:** °C, %RH, ppm, hPa, kΩ (gas resistance).
- **Errors are reported as strings with a path**, e.g. `outputs[1].pin: GPIO 12 strapping pin, may toggle during boot`.

---

## 3. Types

**Files:** `lib/core/src/Types.h`, `Types.cpp`

| Item | Description |
|------|-------------|
| `enum class Quantity { Temperature, Humidity, Co2, Pressure, Gas }` | What can be measured. Names in JSON: `temperature`, `humidity`, `co2`, `pressure`, `gas`. |
| `plausibleRange(q)` | Physical limits; values outside count as a sensor fault: temperature −40…85, humidity 0…100, CO₂ 0…40000, pressure 300…1100, gas 0…100000. |
| `enum class SensorType { Scd41, Bme680, Bme280 }` | JSON names `scd41`, `bme680`, `bme280`. |
| `sensorProvides(type, q)` | SCD41: temperature, humidity, CO₂. BME680: temperature, humidity, pressure, gas. BME280: temperature, humidity, pressure. |
| `Measurement` | `has[q]` + `value[q]` for every quantity. |
| `SensorSample` | Latest reading of one sensor: `ok` (last attempt succeeded), `atMs` (when measured), `failures` (consecutive failures), `m`. |
| `before(a, b)` | Wrap-safe "a is earlier than b". |

To add a new quantity, extend `Quantity`, `kQuantityNames`, `plausibleRange`, `sensorProvides`, the
default `tolerance` array in `Config.h`, and the units in `WebPage.h`.

---

## 4. Pins

**Files:** `lib/core/src/Pins.h`, `Pins.cpp`

```cpp
enum class Chip { Esp32, Esp32S3 };
enum class PinUse { Output, Input, I2c };
const char* pinProblem(Chip chip, int pin, PinUse use);  // nullptr = OK, else the reason
```

Some GPIOs must not drive an SSR:

| Chip | Rejected | Reason |
|------|----------|--------|
| ESP32 | 6–11 | connected to the flash chip |
| | 1, 3 | USB serial console |
| | 34–39 (as output or I²C) | input-only |
| | 0, 2, 5, 12, 15 (as output) | **strapping pins**: read by the boot ROM and can toggle during reset, which could pulse a 230 V load at every reboot |
| | 12 (as I²C) | sets the flash voltage at boot; a pull-up can prevent booting |
| | 20, 24, 28–31, > 39 | do not exist |
| ESP32-S3 | 26–32 | flash/PSRAM |
| | 33–37 | octal PSRAM on R8 modules (e.g. N8R8, N16R8) |
| | 19, 20 | native USB |
| | 43, 44 | USB serial console |
| | 0, 3, 45, 46 (as output) | strapping pins |
| | 22–25, > 48 | do not exist |

`validateConfig` calls this for every output pin and for SDA/SCL. It also rejects a pin used twice.

---

## 5. Config

**Files:** `lib/core/src/Config.h`, `Config.cpp`

### 5.1 Data model

```
Config
├── deviceName                        "climacontrol"  (hostname, mDNS, MQTT client id)
├── wifi { apEnabled, apSsid, apPassword, staSsid, staPassword }
├── web  { user, password }
├── mqtt { enabled, host, port, user, password, baseTopic, publishIntervalS }
├── time { tz, ntpServer }
├── i2c  { sda, scl, muxAddress, freqHz }
├── simulateSensors, staleS, tolerance[quantity], overrideTimeoutS
├── sensors[]  SensorConfig { id, type, group, muxChannel, address, offset[quantity] }
├── outputs[]  OutputConfig { name, label, pin, kind, activeHigh, pwmFreqHz, failsafe,
│                             minOnS, minOffS, maxOnS, cooldownS, rules[] }
│                 Rule { type, signal, on, off, level, windows[], onS, periodS,
│                        inMin, inMax, outMin, outMax }
└── alarms[]   AlarmConfig { signal, min, max, delayS }
```

All keys are listed in [section 20](#20-reference-configuration-keys).

### 5.2 Functions

| Function | What it does |
|----------|--------------|
| `defaultConfig(chip)` | Factory configuration matching the block diagram (6 sensors, 8 outputs, 3 alarms). The default pins and the I²C pins depend on the chip. |
| `parseConfig(json, cfg, errors)` | **Overlays** JSON onto `cfg`. Missing fields keep their value, so `{"mqtt":{"host":"10.0.0.5"}}` changes only the host. **Arrays (`sensors`, `outputs`, `alarms`) are replaced as a whole.** Wrongly typed fields produce errors such as `mqtt.port: expected a whole number in range`. Returns `false` on any error. |
| `validateConfig(cfg, chip)` | Semantic checks, returns a list of errors (empty = OK). See below. |
| `configToJson(cfg, includeSecrets)` | Pretty JSON. With `includeSecrets=false`, non-empty passwords become `"********"`. |
| `parseSignalName("chamber.co2", group, q)` | Splits a signal name. |

**Password mask:** when the web UI loads the config, passwords appear as `********`. When that text is
saved back, `Reader::secret` sees the mask and keeps the stored password. To change a password,
replace the mask with the new value.

**Levels in JSON** (`failsafe`, `level`) accept `true`/`false` as well as numbers 0..1.

**Times** in schedule windows are `"HH:MM"` from `00:00` to `24:00`. A window with `to` < `from` spans
midnight (e.g. 22:00–06:00). `to` is exclusive.

### 5.3 Validation rules

- Names valid and unique (sensor ids, output names); every sensor's group is a valid name.
- Access point: SSID 1–32 characters, password 8–63 characters. If the access point is off, a
  station SSID is required, otherwise the device would be unreachable.
- Web user and password must not be empty.
- MQTT enabled → host required, `wifi.sta` required, port 1–65535, base topic without `+`/`#`.
- I²C: pins pass `pinProblem`, SDA ≠ SCL, multiplexer address 0x70–0x77, frequency 10–400 kHz.
- `staleS` ≥ 5; every tolerance > 0.
- Sensor: mux channel −1..7, address 0x08–0x77, no two sensors on the same channel + address.
- Output: pin safe and unused, PWM frequency 5–40000 Hz, failsafe 0..1, `cooldownS` > 0 if `maxOnS` is set.
- Rule: the signal must exist, meaning some sensor of that group provides that quantity. Level is in (0, 1].
  - `above`: off < on. `below`: off > on. `schedule`: at least one window, from < 24:00, from ≠ to.
  - `cycle`: 0 < onS ≤ periodS. `linear`: in[1] > in[0], out levels within 0..1.
- Alarm: the signal exists, min and/or max is set, and min < max.

### 5.4 Default configuration

Sensors (all on the TCA9548 at 0x70):

| id | type | group | mux channel | address |
|----|------|-------|-------------|---------|
| `chamber_scd41_a` | scd41 | chamber | 0 | 0x62 (98) |
| `chamber_bme680_a` | bme680 | chamber | 0 | 0x77 (119) |
| `chamber_scd41_b` | scd41 | chamber | 1 | 0x62 |
| `chamber_bme680_b` | bme680 | chamber | 1 | 0x77 |
| `inflow` | bme280 | inflow | 2 | 0x76 (118) |
| `ambient` | bme280 | ambient | 3 | 0x76 |

The resulting signals are `chamber.temperature`, `chamber.humidity` (4 sources each), `chamber.co2`
(2), `chamber.pressure`, `chamber.gas` (2), `inflow.temperature/humidity/pressure` (1 each), and the
same for `ambient`.

Outputs and alarms: see [Part 1, section 4.4](01-overview.md#44-default-setup-placeholders). Default
I²C pins are SDA 21 / SCL 22 on the ESP32 and SDA 8 / SCL 9 on the S3. Default output pins are
ESP32 16, 17, 18, 19, 23, 25, 26, 27 and S3 4, 5, 6, 7, 15, 16, 17, 18.

**Tests:** `test_default_config_is_valid`, `test_config_json_roundtrip`, `test_masked_secret_keeps_password`,
`test_partial_update_keeps_other_fields`, `test_parse_reports_type_errors`, `test_validation_catches_mistakes`.

---

## 6. Fusion

**Files:** `lib/core/src/Fusion.h`, `Fusion.cpp`

**Purpose:** turn the readings of several physical sensors into one trustworthy value per signal.

```cpp
struct Signal {
    std::string name;      // "chamber.co2"
    Quantity quantity;
    float value;           // NaN if invalid
    bool valid;            // at least one usable reading
    int sources;           // usable readings this cycle
    int expected;          // configured sensors for this signal
    bool disagree;         // max − min > tolerance
    std::vector<int> sensorIndexes;
};

void applyCalibration(const Config&, std::vector<SensorSample>&);   // adds offsets in place
class Fusion {
    void configure(const Config&);                                  // builds the signal list
    void update(const std::vector<SensorSample>&, uint32_t nowMs);  // recomputes all signals
    const Signal* find(const std::string& name) const;
};
```

**Algorithm for each signal and each cycle:**

1. Collect the readings of its sensors that have the quantity (`has[q]`).
2. Drop readings older than `staleS` (default 30 s).
3. Drop readings outside `plausibleRange` (this also drops NaN).
4. No readings left → `valid = false`, `value = NaN`.
5. Otherwise `value` = **median** (for an even count, the mean of the two middle values).
6. `disagree` = (largest − smallest) > `tolerance[q]`. Defaults: temperature 2 °C, humidity 8 %,
   CO₂ 200 ppm, pressure 5 hPa, gas effectively unlimited.

**Why the median?** One sensor that is far off (drifting, condensation) does not pull the value. With
two sensors the median is their mean, and a large gap is reported as `disagree`, so a person can check.

Note: a sensor's `ok` flag does not matter to fusion, only the age of its last good reading. A sensor
that fails once keeps contributing its last value until it is 30 s old.

**Tests:** `test_fusion_median_and_disagreement`, `test_fusion_drops_stale_and_implausible`, `test_calibration_offset`.

---

## 7. Controller

**Files:** `lib/core/src/Controller.h`, `Controller.cpp`

**Purpose:** decide the level of every output once per tick, and compute the active alarms.

```cpp
class Controller {
    void configure(const Config&, uint32_t nowMs);                     // keeps state of outputs whose name still exists
    void tick(const Fusion&, uint32_t nowMs, int minuteOfDay);         // minuteOfDay −1 = clock not set
    bool setManual(const std::string& name, float level, uint32_t durationS, uint32_t nowMs);  // durationS 0 = default
    bool setAuto(const std::string& name);
    const std::vector<OutputState>& outputs() const;
    const std::vector<Alarm>& alarms() const;
    uint32_t manualRemainingS(const OutputState&, uint32_t nowMs) const;
};
```

### 7.1 Decision per output (in this order)

```
                       ┌─ manual override expired? → back to auto
                       ▼
     mode == manual ──yes──► level = manualLevel                 (reason "manual",  forced)
          │no
     no rules ─────────────► level = 0                           (reason "no rules")
          │
     evaluate every rule ──► any rule lacks input? ──yes──► level = failsafe  (reason "failsafe", forced)
          │no                                                   + alarm output.<name>.failsafe
     level = max(rule levels)                                   (reason "auto")
          │
     relay? level = (level > 0 ? 1 : 0)
          │
     SAFETY TIMING
       in max-on cooldown?                    → level 0         (reason "max-on cooldown")
       on, wants on, on for ≥ maxOnS?         → level 0, start cooldown (reason "max-on")
       not forced and on, wants off, on < minOnS   → keep level (reason "min-on")
       not forced and off, wants on, off < minOffS → stay off   (reason "min-off")
```

Key points:

- **maxOnS also applies in manual mode and failsafe.** A humidifier cannot be left running forever,
  even by hand.
- **minOnS/minOffS do not delay manual commands or failsafe.** A person pressing "off", or a sensor
  failure, acts immediately.
- **Failsafe is per output, not per rule.** If *any* rule of an output lacks its input, the whole
  output goes to its failsafe level. Example: the `outlet` output has CO₂, humidity and cycle rules. If
  the CO₂ signal is lost, `outlet` runs at failsafe (on), even though its humidity rule still works.
  This is deliberately conservative.

### 7.2 Rule evaluation

| Type | Needs | Result |
|------|-------|--------|
| `schedule` | valid clock (`minuteOfDay ≥ 0`) | `level` inside any window, else 0. No clock → no input → failsafe. |
| `cycle` | nothing | `level` during the first `onS` seconds of every `periodS`. Counted from `millis()`, i.e. from boot, not wall-clock time. |
| `above` | valid signal | latch on at `value ≥ on`, latch off at `value ≤ off`; between the two it keeps the previous state (hysteresis). |
| `below` | valid signal | latch on at `value ≤ on`, off at `value ≥ off`. |
| `linear` | valid signal | `out[0] + clamp((v − in[0]) / (in[1] − in[0]), 0, 1) × (out[1] − out[0])`. Note that for a relay output any result > 0 means on. |

The hysteresis latch is stored per rule in `OutputState::latch`. It is reset to off when the
configuration changes.

### 7.3 Manual override

`setManual(name, level, durationS)`: level is clamped to 0..1. `durationS = 0` uses `overrideTimeoutS`
(default 3600 s). If `overrideTimeoutS` is also 0, the override never expires. Expiry is checked at the
start of every tick. `setAuto` ends it immediately.

### 7.4 Reconfiguration

`configure()` keeps the `OutputState` (on/off, manual mode, timers, cooldown) of outputs whose **name**
still exists, so saving a config does not make relays click. New outputs start at their failsafe level.

### 7.5 Alarms

Recomputed every tick (the list contains only currently active alarms):

| Code | Critical | When |
|------|----------|------|
| `signal.<signal>.invalid` | yes | no usable reading |
| `signal.<signal>.degraded` | no | fewer sources than configured sensors |
| `signal.<signal>.disagree` | no | spread > tolerance |
| `output.<name>.failsafe` | yes | output runs at failsafe |
| `output.<name>.max_on` | yes | maxOnS tripped, in cooldown |
| `limit.<signal>.low` / `.high` | yes | configured alarm: value outside min/max continuously for `delayS` (default 300 s) |

**Tests:** `test_hysteresis`, `test_failsafe_when_sensors_missing`, `test_one_sensor_down_is_degraded_not_failsafe`,
`test_schedule_wraps_midnight_and_needs_clock`, `test_cycle`, `test_linear_pwm`, `test_min_on_and_min_off`,
`test_max_on_forces_cooldown`, `test_manual_override_expires`, `test_reconfigure_keeps_state`,
`test_limit_alarm_delay`, `test_millis_wraparound`, `test_default_config_against_simulation`.

---

## 8. Sensirion

**Files:** `lib/core/src/Sensirion.h`, `Sensirion.cpp`

The SCD41 is driven without a library. This module holds the hardware-free part of its protocol.

| Item | Value |
|------|-------|
| Commands (16 bit, sent MSB first) | `0x21B1` start periodic measurement, `0x3F86` stop, `0xEC05` read measurement, `0xE4B8` get data-ready status |
| Response format | 16-bit words, each followed by a CRC byte |
| CRC | CRC-8, polynomial 0x31, init 0xFF (datasheet example: 0xBEEF → 0x92) |
| Conversion | CO₂ = word0 ppm; T = −45 + 175 × word1 / 65535 °C; RH = 100 × word2 / 65535 % |
| Data ready | `(status & 0x07FF) != 0` |

`decodeWords` rejects a response with any wrong CRC, so a disturbed I²C transfer becomes an error
instead of a wrong value.

**Tests:** `test_crc8_datasheet_example`, `test_scd41_decode`.

---

## 9. Status

**Files:** `lib/core/src/Status.h`, `Status.cpp`

`Snapshot` is a self-contained copy of everything the control task knows: uptime, epoch (0 = clock
not set), signals, per-sensor status, per-output status (with `manualRemainingS`) and alarms.
`makeSnapshot(...)` builds it; `snapshotToJson` writes it as JSON with values rounded to one decimal
(levels to two). The format is in [section 21](#21-reference-status-json-alarm-codes-reason-strings).

The snapshot exists so that the network side never touches the controller's live data; it just
copies the latest snapshot under the lock.

---

## 10. Simulation

**Files:** `lib/core/src/Simulation.h`, `Simulation.cpp`

A deliberately simple room model used by the `*_sim` and `wokwi` builds, by `simulateSensors: true`,
and by the 4-hour closed-loop test.

- State: chamber `temperature` (starts at 20 °C), `humidity` (80 %), `co2` (900 ppm).
- Ambient air: 16 °C, 60 %, 420 ppm.
- Outputs are recognised **by name**: a name containing `humid` adds humidity (0.3 %/s at full level);
  names containing `inlet`, `outlet`, `hepa` or `fan` ventilate (pull everything towards ambient); `light`
  adds a little heat. The mushrooms add 1 ppm CO₂ per second, plus a slow temperature drift upwards.
- `fill()` produces one sample per configured sensor with small noise. Chamber-group sensors see the
  chamber state, all others see ambient air.

It is not physically accurate. It only gives the control logic something believable to react to.

---

## 11. App

**Files:** `src/App.h`, `src/App.cpp`

`App` (global instance `app`) owns the configuration and the control task. It is the only bridge
between the network side and the control side.

### 11.1 Thread-safe API (called from web and MQTT)

| Method | Description |
|--------|-------------|
| `config()` | copy of the current config |
| `configVersion()` | counter that increases on every change (MQTT uses it to republish the config) |
| `configSource()` | `"flash"`, `"backup"` or `"defaults"` |
| `snapshot()` | copy of the latest snapshot |
| `credentials(user, pw)` | web login |
| `updateConfig(json, errors, rebootRequired)` | overlay → validate → save to flash → activate. `rebootRequired` is set when device name, WiFi, MQTT or time settings changed, because those are only read at startup. |
| `setOutput(name, mode, level, minutes, error)` | mode `auto`/`on`/`off`/`level`; queues a command; `minutes = 0` means the default timeout |
| `setClock(epoch)` | sets the system clock (only values after Nov 2023 are accepted) |
| `clockValid()` | true if the time is after 2023-11-14 (epoch 1700000000), i.e. set by NTP or by hand |
| `requestReboot()` / `rebootDue()` | reboot one second later, so the HTTP/MQTT answer still gets out |

All of these take a FreeRTOS mutex (`Lock` is a small RAII helper). Commands and config changes are put
in a queue/flag and the control task is woken with `xTaskNotifyGive`, so they take effect within
milliseconds instead of waiting for the next second.

### 11.2 The control task

Created in `begin()`: `xTaskCreatePinnedToCore(controlTask, "control", 12288, this, 3, &task_, 1)`, i.e. a
12 KB stack, priority 3 (above the Arduino loop's 1), pinned to core 1.

```
loop forever:
    under lock: take pending commands; if config changed, copy it
    if config changed: reconfigure outputs, sensors, fusion, controller
    apply commands (setManual / setAuto)
    samples = sensors.poll()            (may block briefly for I²C)
    applyCalibration; fusion.update
    minuteOfDay from local time (−1 if clock not valid)
    controller.tick; outputs.apply
    log on/off transitions (info level)
    build snapshot, store under lock
    feed watchdog
    sleep until 1 s after start, or until woken by a command
```

### 11.3 Config storage

| File on LittleFS | Meaning |
|------------------|---------|
| `/config.json` | active configuration (with passwords) |
| `/config.bak.json` | the previous version |
| `/config.tmp.json` | temporary during a save |

- **Save:** write the temp file and check its size → rename `config.json` to `config.bak.json` →
  rename the temp file to `config.json`. A power cut at any point leaves at least one valid file.
- **Load at startup:** `config.json`, if it parses and validates. Otherwise `config.bak.json`. Otherwise
  factory defaults, which are also written to flash if no `config.json` exists.
- Stored files are overlaid onto the **defaults**, so fields added in a newer firmware get their
  default values.
- If LittleFS cannot be mounted (it is formatted automatically on first use), the device runs on
  defaults.

### 11.4 Watchdog

`esp_task_wdt_init(15, true)` (core 2.x) or `esp_task_wdt_reconfigure` (core 3.x). The control task and
the Arduino loop task both register and must reset it at least every 15 s, or the chip panics and
restarts. After the restart `resetReason` in the status shows `watchdog`.

---

## 12. Sensors

**Files:** `src/Sensors.h`, `src/Sensors.cpp`

### 12.1 Bus layout

```
ESP32 SDA/SCL ── TCA9548 (0x70) ─┬─ ch0: SCD41 0x62, BME680 0x77   (chamber pair A)
                                 ├─ ch1: SCD41 0x62, BME680 0x77   (chamber pair B)
                                 ├─ ch2: BME280 0x76               (inflow)
                                 ├─ ch3: BME280 0x76               (ambient)
                                 └─ ch4–7: free
```

The multiplexer is selected by writing a single byte with one bit per channel (`1 << channel`).
`muxChannel = −1` writes 0 (all channels off) and addresses a device directly on the main bus.

### 12.2 Per-sensor state machine

```
          ┌───────────── not ready ────────────┐
          │  every 5 s: select channel, init() │
          │    ok   → ready                    │
          │    fail → failures++ , log warning │
          └───────────────┬────────────────────┘
                          ▼
          ┌──────────────── ready ─────────────┐
          │  every interval: select, read()    │
          │    Fresh    → ok, failures = 0     │
          │    NotReady → nothing (SCD41 only) │
          │    Error    → failures++;          │
          │               3 in a row → not ready (re-init) │
          └────────────────────────────────────┘
```

| Sensor | Init | Read interval | Read |
|--------|------|---------------|------|
| SCD41 | stop measurement, wait 500 ms (it keeps running across ESP reboots), start periodic measurement; first read 5 s later | 1 s poll of "data ready" (new data every 5 s) | data-ready → read measurement → CRC check (raw I²C, `Sensirion` module) |
| BME280 | `Adafruit_BME280::begin` | 2 s | chip ID register 0xD0 must be 0x60 (the library alone does not notice a missing sensor) |
| BME680 | `Adafruit_BME680::begin` | 3 s (a reading blocks about 200 ms for the gas heater) | chip ID must be 0x61; `performReading`; gas in kΩ |

### 12.3 Bus recovery

If selecting the multiplexer fails 3 times in a row, the bus is assumed hung (typically a slave holding
SDA low after an interrupted transfer):

1. `Wire.end()`; SDA as input with pull-up, SCL as open-drain output.
2. 9 clock pulses on SCL, so a slave can finish the byte it is sending.
3. A STOP condition (SDA low → high while SCL is high).
4. `Wire.begin` again with a 50 ms timeout; all sensors are marked "not ready" and re-initialise.

### 12.4 Simulation mode

With `CC_FORCE_SIMULATION` or `simulateSensors: true`, no I²C is used at all. Every poll advances the
`Simulation` by the elapsed time using the current output levels, and fills the samples. The status
shows `"simulated": true`, and the System tab of the web UI shows the sensors as `SIMULATED`.

---

## 13. Outputs

**Files:** `src/Outputs.h`, `src/Outputs.cpp`

| Function | Behaviour |
|----------|-----------|
| `configure(cfg)` | Sets up every output pin at its **failsafe level**. For a relay, the level is written **before** `pinMode(OUTPUT)`, so the pin never shows a wrong state. Outputs whose hardware (pin, kind, polarity, frequency, LEDC channel) did not change are left alone, so a config save does not interrupt them. Pins no longer used are released (see below). |
| `apply(states)` | Writes each output's level. |
| relay | `digitalWrite`: HIGH when on (or LOW when on with `activeHigh: false`). |
| PWM | LEDC hardware PWM, 10 bit (0–1023), frequency `pwmFreqHz` (default 1000). Inverted for `activeHigh: false`. On core 2.x each PWM output gets an even LEDC channel (0, 2, 4, …), because channels 2n/2n+1 share a timer, so each output can have its own frequency. |
| release | Writes "off", detaches PWM, then sets the pin to an input with a weak pull towards "off" (pull-down for active-high), instead of leaving it floating. |

---

## 14. Network

**Files:** `src/Network.h`, `src/Network.cpp`

- **Access point** (if `wifi.ap.enabled`): SSID/password from config, IP **192.168.4.1**. For people on site.
- **Station** (if `wifi.sta.ssid` is set): joins the farm network for MQTT and NTP. Both can run at once
  (`WIFI_AP_STA`).
- `WiFi.persistent(false)`: credentials come only from our config, not from the ESP's own WiFi storage.
- **Reconnect:** auto-reconnect is off. If the station is offline, the firmware retries every
  **30 s**, because while it searches, clients on the access point see disturbances.
- **Time:** `configTzTime(tz, ntpServer)` starts SNTP and sets the time zone (default `CET-1CEST,M3.5.0,M10.5.0/3`
  = Europe/Berlin with summer time). Without internet the clock stays unset until someone presses
  "Set clock" in the web UI.
- **mDNS:** announces `http://<deviceName>.local` (default `climacontrol.local`) on the farm network.
- `toJson()` adds AP clients, station IP and signal strength (RSSI) to the status.

Network settings are read only at startup, so changing them needs a reboot.

---

## 15. WebUi and WebPage

**Files:** `src/WebUi.h`, `src/WebUi.cpp`, `src/WebPage.h`

### 15.1 HTTP API

| Method | Path | Login | Parameters | Answer |
|--------|------|-------|------------|--------|
| GET | `/` | – | – | the web page |
| GET | `/api/status` | – | – | status JSON ([section 21](#21-reference-status-json-alarm-codes-reason-strings)) |
| GET | `/api/config` | – | – | config, passwords masked |
| POST | `/api/config` | ✔ | JSON body (partial allowed) | result |
| POST | `/api/output` | ✔ | form: `name`, `mode` = auto/on/off/level, `level` 0..1, `minutes` | result |
| POST | `/api/time` | ✔ | form: `epoch` (Unix seconds) | result |
| POST | `/api/reboot` | ✔ | – | result, then reboot after 1 s |
| POST | `/update` | ✔ | multipart file upload of `firmware.bin` | result, then reboot |

A result looks like `{"ok": true}`, `{"ok": true, "rebootRequired": true}` or
`{"ok": false, "errors": ["outputs[0].pin: GPIO 0 strapping pin, may toggle during boot"]}`
(HTTP 400 on failure). Login is HTTP Basic auth with `web.user` / `web.password`, so the browser
shows its login dialog.

Examples with `curl` while connected to the access point:

```bash
curl http://192.168.4.1/api/status
curl -u admin:<web password> -d name=light -d mode=on -d minutes=15 http://192.168.4.1/api/output
curl -u admin:<web password> -H "Content-Type: application/json" \
     -d '{"overrideTimeoutS": 1800}' http://192.168.4.1/api/config
curl -u admin:<web password> -F "firmware=@.pio/build/esp32s3/firmware.bin" http://192.168.4.1/update
```

**Over-the-air update:** the upload handler checks the login on the first chunk, then streams the
file into the inactive app partition via `Update`. Only if everything succeeded does the device
switch to the new partition and reboot. A failed or aborted upload leaves the old firmware running.

### 15.2 The page

`WebPage.h` contains one HTML page with inline CSS and JavaScript, stored in flash (`PROGMEM`). There
are no external files, because there is no internet on the access point. It polls `/api/status` every
2 seconds.

| Tab | Content |
|-----|---------|
| Overview | alarms, then one card per group with its signal values (with source count, disagree warning) |
| Outputs | table with state, mode, reason; buttons Auto / On / Off, % field for PWM; choice of override duration |
| Sensors | each physical sensor: ok/failures, age, raw values |
| Configuration | the config JSON in a text area; Reload / Save; errors are shown as a message |
| System | firmware, chip, reset reason, memory, config source, clock, WiFi, MQTT; buttons: set clock from this device, firmware upload, reboot |

Dark mode follows the phone's setting.

---

## 16. Mqtt

**Files:** `src/Mqtt.h`, `src/Mqtt.cpp`

Library: PubSubClient. Client ID `<deviceName>-<last 6 hex digits of the MAC>`. Keep-alive 30 s, socket
timeout 5 s, buffer 8192 bytes (for incoming config JSON). Outgoing messages are streamed
(`beginPublish`/`write`/`endPublish`), so they are not limited by the buffer.

Only active when `mqtt.enabled` is true **and** the station is connected. Reconnects every 10 s.

### 16.1 Topics (`<base>` = `mqtt.baseTopic`, default `wildnis/climacontrol`)

| Topic | Direction | Retained | Content |
|-------|-----------|----------|---------|
| `<base>/availability` | out | yes | `online` after connect; the broker publishes `offline` (last will) if the device disappears |
| `<base>/status` | out | yes | full status JSON, every `publishIntervalS` (10 s) |
| `<base>/signal/<signal>` | out | yes | single value, e.g. `wildnis/climacontrol/signal/chamber.co2` → `812.4`, or `null` |
| `<base>/alarms` | out | yes | JSON array of active alarms, only when it changes |
| `<base>/config` | out | yes | config without passwords, after connect and on every change |
| `<base>/result` | out | no | answer to each command: `{"ok":true}` / `{"ok":false,"errors":[...]}` |
| `<base>/cmd/output/<name>` | in | **no** | `auto`, `on`, `off`, a number like `0.5`, or `{"mode":"on","minutes":30}` / `{"level":0.4}` |
| `<base>/cmd/config` | in | **no** | partial JSON config, as for `POST /api/config` |
| `<base>/cmd/reboot` | in | **no** | must be exactly `reboot` |

**Do not send commands as retained messages.** The device would execute them again after every
reconnect.

### 16.2 Server stack (prototype)

`server/` contains a Docker Compose stack that receives, stores and displays the controller's data.
It runs on any machine with Docker: a PC for development, later e.g. a Raspberry Pi or a mini PC at
the farm.

```
 controller ──MQTT──► Mosquitto ──► Telegraf ──► InfluxDB ──► Grafana  (http://localhost:3000)
                      broker        collector    database      dashboards
                      :1883         read-only                 
```

| Service | Image | Role | Reachable from |
|---------|-------|------|----------------|
| `mqtt` | `eclipse-mosquitto:2.0.20` | broker; login required; per-user topic rights (`mosquitto.acl`) | network, port 1883 (the controller must connect) |
| `telegraf` | `telegraf:1.33.1` | subscribes to the controller's topics and writes them to InfluxDB (`telegraf.conf`) | nowhere (no ports) |
| `influxdb` | `influxdb:2.7.11` | time-series database, org `wildnis`, bucket `climacontrol`, keeps data forever | this computer only (`127.0.0.1:8086`) |
| `grafana` | `grafana/grafana-oss:11.4.0` | dashboards; data source and dashboard are provisioned from `server/grafana/` | this computer only (`127.0.0.1:3000`) |

```powershell
powershell -ExecutionPolicy Bypass -File server\setup.ps1   # once: creates all secrets
cd server; docker compose up -d                            # start
docker compose down                                        # stop, data kept
docker compose down -v                                     # stop and DELETE all data
```

**Secrets:** `setup.ps1` creates random passwords and tokens in `server/.env` and the hashed MQTT
logins in `server/passwd`. Both are git-ignored, and running the script again keeps existing values.
`docker-compose.yml` passes each container only the secrets it needs.

**MQTT logins and rights** (`mosquitto.acl`):

| User | Rights | Used by |
|------|--------|---------|
| `climacontrol` | read + write `wildnis/#` | the controller (`MQTT_CONTROLLER_PASSWORD`) |
| `telegraf` | **read only** `wildnis/#` | the data collector: it can never send a command |
| `tester` | read + write `wildnis/#` | manual tests with `mosquitto_sub` / `mosquitto_pub` |

**Stored data** (InfluxDB measurements):

| Measurement | Tags | Fields | Source |
|-------------|------|--------|--------|
| `signal` | `device`, `signal` | `value` | `<base>/signal/<signal>` |
| `availability` | `device` | `value` (`online`/`offline`) | `<base>/availability` |
| `output` | `device`, `name` | `level`, `on`, `mode`, `reason` | `outputs` in `<base>/status` |
| `sensor` | `device`, `id`, `type`, `group` | `ok`, `failures`, `values_<quantity>` | `sensors` in `<base>/status` |
| `system` | `device` | `uptime_s`, `free_heap`, `min_free_heap`, `wifi_rssi`, `alarms`, `critical_alarms` | `<base>/status` |

**Dashboard** `ClimaControl` (Grafana home page): connection state, critical and all alarms, time since
the last data; chamber temperature, humidity and CO₂; a timeline of all outputs; the redundant chamber
sensors side by side, to spot a drifting sensor; and WiFi signal, free memory and uptime (a drop means
the controller restarted). Changes made in the Grafana UI are kept in Grafana's own storage. To keep
them in the repository, export the dashboard JSON to `server/grafana/dashboards/climacontrol.json`.

**Not implemented yet:**
- **Alerting.** Nobody is notified yet when the controller goes offline or a critical alarm appears.
  This is the most important missing piece for a critical system. Grafana can send alerts (e-mail,
  Telegram, push and so on) once a notification channel is chosen.
- TLS for MQTT, and running the stack on its final host.

**Connecting the controller:** set this in its configuration, then reboot:
`{"wifi":{"sta":{"ssid":"...","password":"..."}},"mqtt":{"enabled":true,"host":"<server IP>","user":"climacontrol","password":"<MQTT_CONTROLLER_PASSWORD>"}}`.

On Windows, other devices can only reach the broker if the firewall allows TCP port 1883. Limit the
rule to the controller's address and remove it after testing:

```powershell
# in an administrator terminal
New-NetFirewallRule -DisplayName "ClimaControl test MQTT" -Direction Inbound -Protocol TCP -LocalPort 1883 -RemoteAddress <controller IP> -Action Allow
Remove-NetFirewallRule -DisplayName "ClimaControl test MQTT"
```

There is no TLS yet, so logins travel unencrypted on the local network. Use the stack on a trusted
network only.

---

## 17. StatusJson and main.cpp

**`src/StatusJson.cpp`:** `statusJson(snapshot)` = `snapshotToJson` plus a `system` block: device name,
firmware version, chip model, reset reason (`power-on`, `software restart`, `crash`, `watchdog`,
`brownout (supply voltage dropped)`, `reset pin`, …), free and minimum free heap, config source, clock
set, MQTT connected, WiFi details. Used by both the web API and MQTT.

**`src/main.cpp`:** startup order and main loop, see [Part 1, section 7](01-overview.md#7-startup-sequence).
`app.begin()` runs first, so the outputs reach their safe state before any network code runs.

---

## 18. Unit tests

**File:** `test/test_core/test_core.cpp` (Unity). On Windows run it with `.\tools\pio.ps1 test -e native`
(Docker Desktop must be running), because the `native` environment needs a PC `g++`, which Windows does
not have. Where `g++` is installed, `pio test -e native` works directly.

Helpers: `oneSensorConfig()` (two SCD41 in group `chamber`), `relay(...)`, `ruleAbove(...)`, `co2Sample(...)`,
and `Rig`, which wires Config → Fusion → Controller so a test can set readings and call `tick()` at
chosen times.

| Area | Tests |
|------|-------|
| SCD41 protocol | CRC against the datasheet example; decoding and CRC error detection |
| Pins | allowed and rejected pins on both chips |
| Config | defaults valid; JSON round trip; masked password kept; partial update; type errors; five typical mistakes found |
| Fusion | median ignores an outlier and flags disagree; stale and implausible readings dropped; calibration offset |
| Controller | hysteresis; failsafe on missing data; one sensor down = degraded only; schedule across midnight + needs clock; cycle; linear PWM; minOn/minOff; maxOn + cooldown; manual override expiry; reconfigure keeps state; alarm delay; millis wrap-around |
| Closed loop | default config against the simulation for 4 simulated hours (starting at 70 % humidity): after one hour of settling, humidity stays between 80 and 97 % and CO₂ below 1300 ppm; the status JSON is complete |

---

## 19. Tools

| File | Purpose |
|------|---------|
| `tools/Dockerfile` | `python:3.12-bookworm` + PlatformIO 6 |
| `tools/pio.ps1` | builds the image `climacontrol-pio` once, then runs `pio <args>` in it with the project mounted at `/work` and a cache volume `climacontrol-pio-cache`. Building and tests work; flashing needs USB and is done from VS Code. |
| `server/` | server stack prototype: broker, Telegraf, InfluxDB, Grafana (see [16.2](#162-server-stack-prototype)); `setup.ps1` creates its secrets |
| `wokwi.toml` | runs `.pio/build/wokwi/firmware.bin` in the Wokwi simulator and forwards `localhost:8180` to the simulated device's port 80 |
| `diagram.json` | Wokwi wiring: ESP32-S3 DevKitC-1 with 8 LEDs on GPIO 4, 5, 6, 7, 15, 16, 17, 18 (the default outputs) |

---

## 20. Reference: configuration keys

JSON paths as used in the web UI and `cmd/config`. "Reboot" means the change takes effect only after a
restart.

| Key | Type / default | Notes |
|-----|----------------|-------|
| `device.name` | string, `climacontrol` | hostname, mDNS, MQTT client id. Reboot. |
| `wifi.ap.enabled` | bool, `true` | own access point. Reboot. |
| `wifi.ap.ssid` | `ClimaControl` | Reboot. |
| `wifi.ap.password` | `CC_DEFAULT_AP_PASSWORD` from `.env` | 8–63 characters, masked. **Change it.** Reboot. |
| `wifi.sta.ssid` / `.password` | empty | farm WiFi. Reboot. |
| `web.user` / `web.password` | `admin` / `CC_DEFAULT_WEB_PASSWORD` from `.env` | login for all changes. **Change it.** |
| `mqtt.enabled` | `false` | Reboot. |
| `mqtt.host` / `.port` | empty / 1883 | Reboot. |
| `mqtt.user` / `.password` | empty | Reboot. |
| `mqtt.baseTopic` | `wildnis/climacontrol` | Reboot. |
| `mqtt.publishIntervalS` | 10 | status interval |
| `time.tz` | `CET-1CEST,M3.5.0,M10.5.0/3` | POSIX TZ string. Reboot. |
| `time.ntp` | `pool.ntp.org` | Reboot. |
| `i2c.sda` / `.scl` | S3: 8 / 9, ESP32: 21 / 22 | |
| `i2c.muxAddress` | 112 (0x70) | JSON has no hex, write decimal |
| `i2c.freqHz` | 100000 | 10000–400000 |
| `simulateSensors` | `false` | use the room model instead of I²C |
| `fusion.staleS` | 30 | ≥ 5 |
| `fusion.tolerance.<quantity>` | temperature 2, humidity 8, co2 200, pressure 5, gas 1e9 | disagree threshold |
| `overrideTimeoutS` | 3600 | 0 = manual never expires |
| `sensors[].id` | name | unique |
| `sensors[].type` | `scd41` / `bme680` / `bme280` | |
| `sensors[].group` | name | sensors of a group are fused |
| `sensors[].muxChannel` | 0..7, −1 = none | |
| `sensors[].address` | default by type: 98 (0x62), 119 (0x77), 118 (0x76) | |
| `sensors[].offset.<quantity>` | number | calibration, added to the reading |
| `outputs[].name` / `.label` | name / text | |
| `outputs[].pin` | GPIO | checked against `pinProblem` |
| `outputs[].kind` | `relay` / `pwm` | |
| `outputs[].activeHigh` | `true` | `false` for inverting drivers |
| `outputs[].pwmFreqHz` | 1000 | PWM only, 5–40000 |
| `outputs[].failsafe` | 0..1 or bool | level when input is missing |
| `outputs[].minOnS` / `.minOffS` | 0 | anti short-cycling |
| `outputs[].maxOnS` / `.cooldownS` | 0 / 300 | forced pause |
| `outputs[].rules[]` | see below | |
| `alarms[].signal` / `.min` / `.max` / `.delayS` | – / – / – / 300 | |

Rule fields:

```jsonc
{"type": "above",    "signal": "chamber.co2",      "on": 1000, "off": 800, "level": 1}
{"type": "below",    "signal": "chamber.humidity", "on": 85,   "off": 90}
{"type": "schedule", "windows": [{"from": "08:00", "to": "20:00"}, {"from": "22:00", "to": "02:00"}]}
{"type": "cycle",    "onS": 300, "periodS": 1800}
{"type": "linear",   "signal": "chamber.co2", "in": [800, 1500], "out": [0.3, 1]}
```

Because arrays are replaced as a whole, changing one rule means sending the complete `outputs` array.
The simplest way is to edit the full JSON in the Configuration tab.

---

## 21. Reference: status JSON, alarm codes, reason strings

```jsonc
{
  "uptimeS": 3605,
  "epoch": 1793875200,              // null if the clock is not set
  "simulated": false,
  "signals": {
    "chamber.co2":      {"value": 870.0, "sources": 2, "expected": 2},
    "chamber.humidity": {"value": 87.4,  "sources": 3, "expected": 4, "disagree": true},
    "ambient.pressure": {"value": null,  "sources": 0, "expected": 1}
  },
  "sensors": [
    {"id": "chamber_scd41_a", "type": "scd41", "group": "chamber", "ok": true, "failures": 0,
     "ageS": 2, "values": {"temperature": 21.3, "humidity": 88.0, "co2": 865.0}}
  ],
  "outputs": [
    {"name": "humidifier", "label": "Breeding humidifier", "kind": "relay", "on": true, "level": 1,
     "mode": "auto", "reason": "auto"},
    {"name": "light", "label": "Breeding light", "kind": "relay", "on": true, "level": 1,
     "mode": "manual", "reason": "manual", "manualRemainingS": 840}
  ],
  "alarms": [
    {"code": "signal.chamber.humidity.degraded", "message": "chamber.humidity: only 3 of 4 sensors deliver data", "critical": false}
  ],
  "system": {
    "device": "climacontrol", "firmware": "0.1.0", "chip": "ESP32-S3", "resetReason": "power-on",
    "freeHeap": 210000, "minFreeHeap": 190000, "configSource": "flash", "clockSet": true,
    "mqttConnected": false,
    "wifi": {"ap": {"ssid": "ClimaControl", "ip": "192.168.4.1", "clients": 1}}
  }
}
```

(Values are illustrative.)

**Output `reason` values:**

| reason | Meaning |
|--------|---------|
| `start` | before the first tick |
| `auto` | rules decided |
| `no rules` | output has no rules (only manual control) → off |
| `manual` | manual override |
| `failsafe` | a rule's input is missing → failsafe level |
| `min-on` | wants off, but minimum on-time not reached |
| `min-off` | wants on, but minimum off-time not reached |
| `max-on` | maximum on-time reached → forced off now |
| `max-on cooldown` | in the forced pause after max-on |

Alarm codes: see [section 7.5](#75-alarms).
