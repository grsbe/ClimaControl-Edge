# Part 3 – Beginner guide: from zero to understanding ClimaControl

Part 3 of the ClimaControl documentation. It assumes **no experience with microcontrollers or
electronics**. Some general computer knowledge helps, and a little programming experience in any
language is enough.

[Part 1](01-overview.md) explains what the system does, and [Part 2](02-building-blocks.md) is the
detailed reference. Come back to those once this guide makes sense.

## How to use this guide

- Chapters 1–4 explain what a microcontroller is and how a program gets onto it. Read them in order.
- Chapter 5 has small programs to try yourself on an ESP32 board.
- Chapter 6 is a short C++ course covering only what this project uses.
- Chapter 7 explains the techniques the firmware uses (I²C, PWM, tasks, watchdog, MQTT, …).
- Chapter 8 walks through the actual ClimaControl code, following one second of its life.
- Chapter 9 has hands-on exercises with the real firmware, including writing a test.
- Chapter 10 helps when something goes wrong. Chapter 11 is a glossary.

## Contents

1. [What is a microcontroller?](#1-what-is-a-microcontroller)
2. [Just enough electronics](#2-just-enough-electronics)
3. [How a program gets onto the chip](#3-how-a-program-gets-onto-the-chip)
4. [The tools: VS Code and PlatformIO](#4-the-tools-vs-code-and-platformio)
5. [Your first programs](#5-your-first-programs)
6. [C++ in one chapter](#6-c-in-one-chapter)
7. [The techniques ClimaControl uses](#7-the-techniques-climacontrol-uses)
8. [Guided tour: one second in the life of ClimaControl](#8-guided-tour-one-second-in-the-life-of-climacontrol)
9. [Hands-on exercises with the real firmware](#9-hands-on-exercises-with-the-real-firmware)
10. [When something goes wrong](#10-when-something-goes-wrong)
11. [Glossary](#glossary)

---

## 1. What is a microcontroller?

A **microcontroller** is a complete small computer on a single chip: processor, memory, storage and
connections to the outside world. Compared with a PC:

| | PC | ESP32-S3 (example: N8R8 module) |
|---|----|------|
| Processor | several cores at about 3–5 GHz | 2 cores at 240 MHz |
| Working memory (RAM) | 8–64 GB | 512 KB internal, plus optional PSRAM (8 MB: the "R8" in N8R8) |
| Storage | SSD, 500 GB+ | 4–16 MB flash memory (8 MB: the "N8" in N8R8) |
| Operating system | Windows / Linux | none, or a tiny one (FreeRTOS) |
| Runs | many programs | **exactly one program**, the firmware |
| Starts in | 30 s or more | well under a second |
| Power | 50–500 W | about 0.5 W |
| Price | hundreds of euros | about 5 euros |
| Special skill | screen, keyboard | **pins**: it can switch things on and off, read sensors, talk to other chips |

A microcontroller does one job forever. When it gets power, it starts the program in its flash memory
and never stops running it. There is no desktop, no file browser and no "shut down". You can simply
pull the plug.

### The ESP32 family

The **ESP32** by Espressif is popular because it is cheap, fast, and has **WiFi and Bluetooth built
in**. There are several versions. This project supports two:

- **ESP32** (classic), e.g. on the ESP32-DevKitC-32E board;
- **ESP32-S3**, newer, e.g. on the ESP32-S3-DevKitC-1 board and its clones. The name of the module
  tells its memory: **N8R8** means 8 MB flash and 8 MB PSRAM.

The code is almost identical for both. What differs is which pin numbers are allowed for what (see
[Part 2, section 4](02-building-blocks.md#4-pins)). The exact supported boards, their default pins and
their test status are listed in [Part 1, section 2.2](01-overview.md#22-supported-boards).

### Chip, module, development board

```
 ┌──────────────── development board ("DevKit") ────────────────┐
 │                                                              │
 │   ┌──── module (metal can) ────┐     USB-C "UART"  ◄── use this one
 │   │  ESP32-S3 chip + flash +   │     USB-C "USB"
 │   │  PSRAM + antenna           │     USB-serial chip (e.g. CH343, CP2102)
 │   └────────────────────────────┘     voltage regulator 5 V → 3.3 V
 │                                      BOOT button   RST button   RGB LED
 │  ● ● ● ● ● ● ● ● ● ● ● ● ● ● ● ● ● ● ● ● ●   ← pin header row (GPIOs, 3V3, 5V, GND)
 └──────────────────────────────────────────────────────────────┘
```

- The **chip** is the actual microcontroller.
- The **module** (the silver can) contains the chip, the flash memory and the antenna.
- The **development board** adds USB, a voltage regulator, buttons and pin headers, so you can use it
  on a desk without soldering.
- **RST** (or EN) restarts the chip. **BOOT**, held while pressing RST, puts it into "receive new
  firmware" mode. Normally the PC does this automatically.

In the final ClimaControl box, the ESP32 sits on the custom board from `hardware.jpg`, either as a
module or as a plugged-in DevKit. The software is the same.

---

## 2. Just enough electronics

You do not need to be an electrician to work on the firmware. You do need these few ideas.

### 2.1 Voltage, ground, HIGH and LOW

- **Voltage** is like water pressure. The ESP32 runs on **3.3 V**.
- **GND** (ground) is the 0 V reference. Everything connected must share the same GND.
- A pin can be **HIGH** (3.3 V, logic 1) or **LOW** (0 V, logic 0). That is all digital electronics is.

> ⚠️ **Never** connect 5 V (or anything higher) to an ESP32 pin. The pins tolerate 3.3 V only.
> **Never** touch the 230 V side of the SSRs while the box is connected to mains. The firmware only
> ever deals with the 3.3 V side.

### 2.2 GPIO: the pins you control

**GPIO** means *General Purpose Input/Output*. Each one has a number (GPIO 4, GPIO 15, …) and can be:

- an **output**: the program sets it HIGH or LOW, e.g. to light an LED or drive an SSR;
- an **input**: the program reads whether something outside makes it HIGH or LOW, e.g. a button.

A pin can deliver only a few milliamps, enough for an LED or an SSR's control input, but **not** for a
motor or lamp. That is why there are relays.

### 2.3 SSR: how 3.3 V switches 230 V

A **solid-state relay** (SSR) is an electronic switch. On its input side is an LED (light). On its output
side is a light-sensitive switch for mains voltage. When the ESP32 sets a pin HIGH, the LED inside lights
up and the 230 V side conducts: the humidifier runs. The two sides are electrically separated, so the
ESP32 never touches mains.

```
 ESP32 GPIO ──►[ SSR input: LED ] ~~~ light ~~~ [ SSR output: switch ]── 230 V ── humidifier
 ESP32 GND  ──►[                ]               [                    ]── N
```

### 2.4 Floating pins and pull-down resistors

While the ESP32 starts (about half a second), its pins are not yet configured. They are **floating**:
not HIGH, not LOW, picking up random electrical noise. An SSR might then briefly switch on.

A **pull-down resistor** (e.g. 10 kΩ from the pin to GND) gently holds the line LOW whenever nobody
drives it. The ESP32 easily overrides it when it sets the pin HIGH. **The real board needs one on every
SSR input.** The firmware cannot fix this, because the problem happens before the firmware runs.

### 2.5 Strapping pins

A few pins are read by the chip at the moment it starts, to decide how to boot. They are called
**strapping pins** (on the S3: GPIO 0, 3, 45, 46). They may wiggle during reset, so an SSR on such a
pin could pulse the load at every restart. The firmware refuses to use them for outputs.

---

## 3. How a program gets onto the chip

```
  your code (.cpp, .h)       compiler (on your PC)          firmware.bin              ESP32 flash
  ───────────────────  ───►  translates C++ into the  ───►  (about 1 MB of     ───►   stored permanently,
  human-readable             machine code of the            machine code)     USB    runs at every power-on
                             ESP32 ("cross-compiling")                       "flashing"
```

1. **Write** the code on your PC in C++.
2. **Compile:** a compiler turns the text into machine instructions for the ESP32's processor. Your PC
   has a different processor, so this is called *cross-compiling*. The result is `firmware.bin`.
3. **Flash:** a tool (`esptool`) sends the file over USB into the ESP32's flash memory. "Flash" here is
   the name of the memory type, the same as in a USB stick.
4. **Run:** the chip restarts and runs the new program. It stays there across power cuts until you
   flash something else.

### Flash vs RAM

- **Flash** (8 MB): permanent. Holds the firmware and a small file system. ClimaControl keeps its
  settings there in `/config.json`.
- **RAM**: fast but forgotten when power goes off. Holds the current sensor values, output states, and so on.

That is why ClimaControl forgets the **time of day** after a power cut, because the clock lives in RAM,
but not its **settings**.

### The serial monitor: the chip talks back

The board has no screen. Through the same USB cable, the program can send text to the PC:
`Serial.println("hello")`. The **serial monitor** in VS Code shows it. This is the main tool for
seeing what the firmware is doing. Both sides must use the same speed (**baud rate**); here it is
**115200**. A wrong speed shows garbage characters.

### Two ways to update the firmware

- **By USB cable** (always works, used during development);
- **Over the air (OTA)** through the web page: *System → Firmware update*. The new firmware is written
  into a second slot in flash, and only if it arrived completely does the chip switch to it. A failed
  upload leaves the old firmware running.

---

## 4. The tools: VS Code and PlatformIO

### 4.1 What you install

| Tool | Purpose |
|------|---------|
| **VS Code** | the code editor |
| **PlatformIO IDE** (VS Code extension) | downloads compiler, ESP32 libraries and flash tool automatically; build/upload/monitor buttons |
| USB driver | usually installed by Windows automatically. The board's USB-serial chip (e.g. **CH343**, **CH340** or **CP2102**, printed on the chip next to the USB port) appears as a COM port, e.g. COM3. |
| optional: **Wokwi Simulator** extension | runs the firmware in a simulated ESP32 without a board |
| **Docker Desktop** | runs the unit tests on Windows (`tools\pio.ps1 test -e native`); can also build without PlatformIO installed |

### 4.2 The project file `platformio.ini`

It tells PlatformIO which board, framework and libraries to use. Each `[env:...]` section is one
**environment**, a build variant. Here is a shortened version:

```ini
[esp32_common]
platform = espressif32@^6.9.0        ; ESP32 support, version 6.9 or newer 6.x
framework = arduino                  ; use the Arduino programming style
monitor_speed = 115200               ; serial monitor speed
lib_deps =                           ; libraries downloaded automatically
    knolleary/PubSubClient@^2.8
    adafruit/Adafruit BME280 Library@^2.2.4

[env:esp32s3]                        ; the real controller on an ESP32-S3
extends = esp32_common
board = esp32-s3-devkitc-1

[env:esp32s3_sim]                    ; same, but sensors are simulated
extends = env:esp32s3
build_flags = ${esp32_common.build_flags} -DCC_FORCE_SIMULATION=1
```

`-DCC_FORCE_SIMULATION=1` defines a name the code can check with `#ifdef`. The `*_sim` builds use it to
replace the real sensors with a small room simulation.

### 4.3 The buttons

At the bottom of VS Code, PlatformIO shows a status bar:

| Symbol | Action |
|--------|--------|
| ✓ | **Build** (compile only) |
| → | **Upload** (compile + flash) |
| 🔌 | **Serial monitor** |
| 🗑 | Clean (delete build results) |
| `env:…` | choose the environment, e.g. `esp32s3_sim` |

The first build downloads about 1 GB of tools and takes a few minutes. Later builds take seconds to a
minute.

### 4.4 Flashing ClimaControl onto your board

1. Plug the board in with a **data** USB cable (some cables only charge) using the port labelled
   **UART** or **COM**.
2. In VS Code, choose environment `env:esp32s3_sim` (no sensors needed).
3. Click **→ Upload**. You will see `Connecting....` and then percentages. At the end: `SUCCESS`.
4. Click **🔌 Monitor**. Press **RST** on the board to see the boot messages.
5. On your phone, join the WiFi **ClimaControl** (password `pilzezucht`) and open **http://192.168.4.1**.

If the upload hangs at `Connecting...`: hold **BOOT**, press and release **RST**, release **BOOT**, and try
again.

---

## 5. Your first programs

Trying small programs is the fastest way to understand the ideas. **Use a board that is not
installed in a controller, and a separate project:** uploading any of these replaces ClimaControl on
that board. You can put ClimaControl back at any time with *Upload* in this project.

**Create a playground project:** PlatformIO Home → *New Project* → name `playground`, board
*Espressif ESP32-S3-DevKitC-1*, framework *Arduino*. Then set its `platformio.ini` to:

```ini
[env:esp32s3]
platform = espressif32@^6.9.0
board = esp32-s3-devkitc-1
framework = arduino
monitor_speed = 115200
```

Each example below replaces `src/main.cpp` of the playground.

### 5.1 The skeleton of every Arduino program

```cpp
#include <Arduino.h>      // the Arduino toolbox: pinMode, digitalWrite, delay, Serial, ...

void setup() {
    // runs ONCE after power-on or reset
}

void loop() {
    // runs AGAIN AND AGAIN, forever, as fast as possible
}
```

There is no `main()` to write: the Arduino framework calls `setup()` once and then `loop()` endlessly.
ClimaControl's `src/main.cpp` has exactly these two functions.

### 5.2 Hello world over the serial monitor

```cpp
#include <Arduino.h>

void setup() {
    Serial.begin(115200);              // open the USB serial connection at 115200 baud
    Serial.println("Hello from the ESP32-S3!");
}

void loop() {
    Serial.print("I have been running for ");
    Serial.print(millis() / 1000);     // millis(): milliseconds since start
    Serial.println(" seconds");
    delay(1000);                       // wait 1000 ms = 1 second
}
```

Upload, open the monitor, and you see a new line every second. Press RST and the counter restarts at
0: the program starts from the beginning at every reset.

### 5.3 Blink an LED, your first output

**Wiring** (breadboard): GPIO 4 → 220 Ω resistor → LED long leg; LED short leg → GND. The resistor
limits the current. Without it the LED or the pin can be damaged.

```
 GPIO 4 ──[220 Ω]──►|── GND
                   LED (long leg on the resistor side)
```

```cpp
#include <Arduino.h>

const int LED_PIN = 4;                 // the same pin ClimaControl uses for OUT1 (hepa)

void setup() {
    pinMode(LED_PIN, OUTPUT);          // make the pin an output
}

void loop() {
    digitalWrite(LED_PIN, HIGH);       // 3.3 V → LED on
    delay(500);
    digitalWrite(LED_PIN, LOW);        // 0 V → LED off
    delay(500);
}
```

If you replaced the LED with an SSR input, this program would switch a 230 V lamp on and off every half
second. That is exactly what ClimaControl's `Outputs` module does, only driven by rules.

### 5.4 Doing two things at once: `millis()` instead of `delay()`

`delay(500)` makes the chip do **nothing** for 500 ms. A controller that must also answer web requests
cannot afford that. The usual solution is to check the clock and act when it is time:

```cpp
#include <Arduino.h>

const int LED_PIN = 4;
uint32_t lastToggle = 0;               // when we last switched the LED
bool ledOn = false;

void setup() {
    Serial.begin(115200);
    pinMode(LED_PIN, OUTPUT);
}

void loop() {
    uint32_t now = millis();
    if (now - lastToggle >= 500) {     // 500 ms passed? (this form survives the 49-day wrap-around)
        lastToggle = now;
        ledOn = !ledOn;
        digitalWrite(LED_PIN, ledOn ? HIGH : LOW);
    }

    // ...meanwhile loop() keeps running and can do other work:
    if (Serial.available()) {
        char c = Serial.read();
        Serial.printf("You typed '%c' while the LED keeps blinking\n", c);
    }
}
```

`millis()` is a 32-bit number and starts at 0 again after about 49.7 days. Writing `now - lastToggle >= 500`
(and **not** `now >= lastToggle + 500`) keeps working across that wrap-around. ClimaControl uses this
everywhere, and has a test for it.

### 5.5 Reading an input: a button

**Wiring:** a push button between GPIO 10 and GND. No resistor needed: we switch on the chip's internal
pull-**up**, so the pin reads HIGH when the button is open and LOW when pressed.

```cpp
#include <Arduino.h>

const int BUTTON_PIN = 10;
const int LED_PIN = 4;

void setup() {
    pinMode(BUTTON_PIN, INPUT_PULLUP);   // input, held HIGH by an internal resistor
    pinMode(LED_PIN, OUTPUT);
}

void loop() {
    bool pressed = digitalRead(BUTTON_PIN) == LOW;
    digitalWrite(LED_PIN, pressed ? HIGH : LOW);
}
```

This is the pull resistor idea from chapter 2.4. ClimaControl uses the same mechanism when it gives up
a pin: `pinMode(pin, INPUT_PULLDOWN)`.

### 5.6 Dimming: PWM

A digital pin can only be fully on or off. To get "50 %", switch it on and off very fast and vary the
share of time it is on. That is **PWM** (pulse-width modulation). An LED looks half as bright, and a
DC fan runs at about half speed.

```
 100 % ████████████████████████
  50 % ████    ████    ████
  25 % ██      ██      ██
       ◄─ one period, e.g. 1 ms at 1000 Hz ─►
```

The ESP32 has a hardware PWM unit called **LEDC**. It runs by itself, so the program only sets the
value. With the core version used here (Arduino-ESP32 2.x):

```cpp
#include <Arduino.h>

const int LED_PIN = 4;
const int CHANNEL = 0;        // LEDC channel 0..7
const int BITS = 10;          // resolution: values 0..1023

void setup() {
    ledcSetup(CHANNEL, 1000, BITS);   // channel 0: 1000 Hz, 10 bit
    ledcAttachPin(LED_PIN, CHANNEL);  // connect the pin to the channel
}

void loop() {
    for (int duty = 0; duty <= 1023; duty += 8) {   // fade in
        ledcWrite(CHANNEL, duty);
        delay(5);
    }
    for (int duty = 1023; duty >= 0; duty -= 8) {   // fade out
        ledcWrite(CHANNEL, duty);
        delay(5);
    }
}
```

ClimaControl's `fan_pwm_1` output does the same: a level of 0.3 becomes `0.3 × 1023 ≈ 307`.

### 5.7 Talking to a sensor: I²C

Once you have a sensor, here is a program that lists every I²C device it finds behind the TCA9548
multiplexer. It is useful to check the wiring before running ClimaControl.

```cpp
#include <Arduino.h>
#include <Wire.h>

const int SDA_PIN = 8, SCL_PIN = 9;     // ClimaControl's default I²C pins on the S3
const uint8_t MUX = 0x70;               // TCA9548 address

bool selectChannel(int ch) {            // switch the multiplexer to one channel
    Wire.beginTransmission(MUX);
    Wire.write(1 << ch);                // one bit per channel: ch 2 → 0b00000100
    return Wire.endTransmission() == 0; // 0 = the multiplexer answered
}

void setup() {
    Serial.begin(115200);
    delay(1000);
    Wire.begin(SDA_PIN, SCL_PIN, 100000);   // 100 kHz bus speed

    for (int ch = 0; ch < 8; ch++) {
        if (!selectChannel(ch)) {
            Serial.println("Multiplexer does not answer - check wiring and power");
            return;
        }
        for (uint8_t addr = 0x08; addr < 0x78; addr++) {
            if (addr == MUX) continue;
            Wire.beginTransmission(addr);
            if (Wire.endTransmission() == 0)
                Serial.printf("channel %d: device at 0x%02X\n", ch, addr);
        }
    }
    Serial.println("scan done");
}

void loop() {}
```

Expected with the default wiring: channel 0 and 1 show `0x62` (SCD41) and `0x77` (BME680); channel 2
and 3 show `0x76` (BME280).

### 5.8 Reading a sensor with a library

Most sensors come with a library that hides the byte-level details. Add
`lib_deps = adafruit/Adafruit BME280 Library@^2.2.4` to the playground's `platformio.ini`, then:

```cpp
#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_BME280.h>

Adafruit_BME280 bme;

void setup() {
    Serial.begin(115200);
    Wire.begin(8, 9);
    // (if the sensor is behind the multiplexer, select its channel first, see 5.7)
    if (!bme.begin(0x76, &Wire)) {
        Serial.println("BME280 not found");
        while (true) delay(1000);
    }
}

void loop() {
    Serial.printf("%.1f °C   %.1f %%RH   %.1f hPa\n",
                  bme.readTemperature(), bme.readHumidity(), bme.readPressure() / 100.0f);
    delay(2000);
}
```

ClimaControl's `Sensors.cpp` does exactly this for the BME280 and BME680. For the SCD41 it speaks the
protocol directly, which you will see in chapter 8.

---

## 6. C++ in one chapter

The firmware is written in C++. Here is only what you need to read this code.

### 6.1 Variables and types

```cpp
int count = 5;              // whole number (32 bit on the ESP32)
uint32_t ms = 1000;         // unsigned 32 bit: 0 .. 4294967295 (used for millis())
uint8_t address = 0x62;     // unsigned 8 bit: 0..255. 0x62 is hexadecimal = 98 decimal
float humidity = 87.5f;     // decimal number; the 'f' marks it as float
bool on = true;             // true / false
const int kTickMs = 1000;   // const: can never change. The 'k' prefix is a naming habit for constants
std::string name = "hepa";  // text
```

### 6.2 Functions

```cpp
// return type, name, parameters
float average(float a, float b) {
    return (a + b) / 2;
}
float x = average(80.0f, 90.0f);   // 85
```

**References (`&`)** let a function change the caller's variable, or avoid copying big objects:

```cpp
void addOffset(float& value, float offset) { value += offset; }   // changes the original
float t = 20; addOffset(t, -1.5f);                                 // t is now 18.5

bool parseConfig(const std::string& json, Config& cfg, std::vector<std::string>& errors);
//               ^ read-only, not copied  ^ filled in    ^ filled in with error messages
```

This pattern ("return true/false, deliver results through reference parameters") appears all over
ClimaControl.

### 6.3 `struct` and `class`: bundling data

```cpp
struct SensorSample {       // from lib/core/src/Types.h
    bool ok = false;        // default values
    uint32_t atMs = 0;
    uint16_t failures = 0;
    Measurement m;
};

SensorSample s;             // create one
s.ok = true;                // access a member with '.'
```

A `class` is the same, but members are private unless listed under `public:`. Classes usually also
have **methods** (functions that belong to them):

```cpp
class Fusion {
   public:                                     // usable from outside
    void configure(const Config& cfg);
    const Signal* find(const std::string& name) const;   // trailing const: does not modify the object
   private:                                    // internal
    std::vector<Signal> signals_;              // trailing '_' marks member variables here
};
```

### 6.4 `std::vector`: a growable list

```cpp
std::vector<float> values;      // empty list of floats
values.push_back(812.0f);       // append
values.push_back(830.0f);
values.size();                  // 2
values[0];                      // 812
for (float v : values) { ... }  // loop over all elements
```

### 6.5 `enum class`: named choices

```cpp
enum class RuleType { Above, Below, Schedule, Cycle, Linear };
RuleType t = RuleType::Below;
switch (t) {
    case RuleType::Above: /* ... */ break;
    case RuleType::Below: /* ... */ break;
    default: break;
}
```

### 6.6 Pointers, briefly

A pointer (`*`) holds the *address* of something, or `nullptr` for "nothing". In this code they mainly
mean "may be missing":

```cpp
const Signal* sig = fusion.find("chamber.co2");
if (!sig || !sig->valid) { /* not found, or no data */ }   // '->' accesses members through a pointer
float v = sig->value;
```

### 6.7 Header (`.h`) and source (`.cpp`) files

- The **header** declares what exists: "there is a class `Fusion` with a method `update`".
- The **source file** contains the actual code.
- `#include "Fusion.h"` pastes a header in, so other files can use it.
- `#pragma once` at the top prevents a header from being included twice.

### 6.8 Namespaces

```cpp
namespace cc {               // everything in lib/core lives in "cc" (ClimaControl)
    Config defaultConfig(Chip chip);
}
cc::Config c = cc::defaultConfig(cc::Chip::Esp32S3);   // used from outside with the prefix
```

### 6.9 The preprocessor: `#if`, `#ifdef`, `#define`

Lines starting with `#` are handled before compiling. They switch code on or off:

```cpp
#ifdef CC_FORCE_SIMULATION       // defined by -DCC_FORCE_SIMULATION=1 in platformio.ini
    simulate_ = true;            // only compiled into *_sim builds
#else
    simulate_ = cfg.simulateSensors;
#endif

#if CONFIG_IDF_TARGET_ESP32S3    // set automatically when building for an S3
constexpr cc::Chip kChip = cc::Chip::Esp32S3;
#endif
```

### 6.10 Lambdas: small inline functions

```cpp
server_.on("/api/status", HTTP_GET, [this]() {          // "when someone opens /api/status, run this"
    sendJson(200, statusJson(app.snapshot()));
});
```

`[this]` lets the lambda use the surrounding object's members. They are used for web routes and as
small helpers inside functions.

### 6.11 `std::unique_ptr`

An owning pointer that deletes its object automatically. Used in `Sensors.cpp` for the sensor driver
objects: `d.bme280.reset(new Adafruit_BME280());`.

---

## 7. The techniques ClimaControl uses

### 7.1 I²C, the sensor bus

**I²C** ("I-squared-C") connects many chips with just **two wires** (plus power and GND):

- **SDA**: data, **SCL**: clock;
- every device has an **address** (7 bit, e.g. SCD41 = 0x62, BME280 = 0x76);
- the ESP32 is the *controller*: it starts every conversation. Sensors only answer.

```
 ESP32 ── SDA ─┬──────────┬──────────┬────
       ── SCL ─┼┬─────────┼┬─────────┼┬───
               ││         ││         ││
            [SCD41]    [BME680]   [BME280]
             0x62        0x77       0x76
```

A conversation looks like: "device 0x62, here is command 0xEC05" → "device 0x62, give me 9 bytes" → 9
bytes arrive.

**The problem:** two identical sensors have the **same address**. You cannot put both SCD41s on one bus.

**The solution: the TCA9548 multiplexer**, a switch with 8 outputs ("channels"). The ESP32 tells it
which channel to connect, then talks to the sensors there:

```
                         ┌─ ch0 ─ SCD41 0x62 + BME680 0x77  (chamber A)
 ESP32 ── TCA9548 0x70 ──┼─ ch1 ─ SCD41 0x62 + BME680 0x77  (chamber B)
                         ├─ ch2 ─ BME280 0x76  (inflow)
                         └─ ch3 ─ BME280 0x76  (ambient)
```

A second benefit: a broken sensor on channel 2 cannot block the others, because it is disconnected
while the others are read.

**Stuck bus:** if a transfer is interrupted (e.g. by a reset), a sensor can keep holding SDA low and
the whole bus freezes. The fix is to send 9 clock pulses so the sensor finishes its byte, then a STOP
signal. `Sensors::recoverBus()` does exactly this.

### 7.2 Checksums (CRC)

Long cables and electrical noise can flip bits. The SCD41 sends a **CRC** byte after every 2 data
bytes, a fingerprint computed from them. The firmware recomputes it, and if it does not match, the
reading is thrown away instead of being used. See `lib/core/src/Sensirion.cpp`.

### 7.3 Hysteresis: why there are two thresholds

If the humidifier switched on below 88 % and off above 88 %, it would flicker on and off whenever the
humidity hovers around 88 %. Relays and devices wear out from that. Instead there are **two**
thresholds:

```
 humidity
   90 % ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─  off threshold
              ╱╲            ╱‾‾╲
   85 % ─ ─ ─╱─ ╲─ ─ ─ ─ ─ ╱─ ─ ╲─ ─ ─ ─ ─ ─  on threshold
        ╲   ╱    ╲    ╱‾‾‾╱      ╲
         ╲_╱      ╲__╱            ╲___
 humidifier: ON──────┐ off │ON───────────┐ off
```

It switches on at ≤ 85 %, off at ≥ 90 %, and in between keeps doing what it was doing.

### 7.4 Redundancy and the median

Every important value is measured by more than one sensor. The firmware takes the **median** (sort
the values and take the middle one), not the average:

- readings 85, 86, 99 → average 90 (pulled by the faulty 99), **median 86** (ignores it).

Readings that are older than 30 s or impossible (humidity 140 %) are ignored. If no reading is left, the
value is **invalid**, and the outputs depending on it go to their safe **failsafe** level.

### 7.5 Tasks (FreeRTOS): doing several things at once

The ESP32 runs a tiny operating system, **FreeRTOS**. It can run several **tasks**, which are like
programs that share the processor (and the S3 has 2 cores).

ClimaControl has two tasks:

- the **control task**: sensors → decisions → outputs, every second;
- the **network loop**: WiFi, web, MQTT (this is the Arduino `loop()`).

**Why?** If the network code hangs (a slow phone, a broken MQTT server), the climate control must keep
running. Each task runs on its own.

**Sharing data safely: the mutex.** If both tasks touched the same data at the same moment, one could
read half-updated values. A **mutex** is like the single key to a room: whoever holds it may enter, and
everybody else waits. In the code:

```cpp
{
    Lock lock(mutex_);     // take the key (wait if someone else has it)
    snapshot_ = snap;      // safe: nobody else is in the room
}                          // leaving the { } block gives the key back automatically
```

### 7.6 The watchdog

A **watchdog** is a hardware timer that must be reset ("fed") regularly. If the program freezes and
stops feeding it, the watchdog **restarts the chip**. ClimaControl uses 15 seconds:

```cpp
esp_task_wdt_reset();   // "I'm alive", called every loop
```

After such a restart, the System tab shows reset reason `watchdog`.

### 7.7 JSON: the configuration format

All settings are stored and exchanged as **JSON**, a text format readable by humans and programs:

```json
{
  "outputs": [
    {
      "name": "humidifier",
      "pin": 5,
      "failsafe": 0,
      "maxOnS": 1800,
      "rules": [ {"type": "below", "signal": "chamber.humidity", "on": 85, "off": 90} ]
    }
  ]
}
```

`{ }` is an object with named fields, `[ ]` is a list. The firmware reads and writes it with the
**ArduinoJson** library.

### 7.8 WiFi: access point and station

- **Access point (AP):** the ESP32 *creates* a WiFi network (`ClimaControl`) like a small router. Phones
  connect to it directly, with no internet involved. The ESP32 is at `192.168.4.1`.
- **Station (STA):** the ESP32 *joins* an existing network (the farm WiFi), like a laptop does. Needed
  to reach an MQTT server and to get the time from the internet (NTP).

ClimaControl can do both at once.

### 7.9 HTTP: the web interface

The ESP32 runs a small **web server**. When you open `http://192.168.4.1`, your browser sends
`GET /` and the ESP32 answers with the web page. The page's JavaScript then asks
`GET /api/status` every 2 seconds and gets JSON back. Buttons send `POST` requests, for example
`POST /api/output` with `name=light&mode=on`. Changes require the login.

### 7.10 MQTT: talking to a server

**MQTT** is a messaging system for devices. Everyone connects to a central **broker** (e.g. Mosquitto),
which works like a post office with named mailboxes called **topics**:

```
 ClimaControl ── publish "wildnis/climacontrol/signal/chamber.co2" = 870 ──► BROKER ──► server, dashboard, phone app
 server       ── publish "wildnis/climacontrol/cmd/output/light"  = on  ──► BROKER ──► ClimaControl (subscribed to cmd/#)
```

- **publish** = send a message to a topic; **subscribe** = receive everything on a topic;
- `#` is a wildcard: `cmd/#` means "everything under cmd/";
- **retained** messages are kept by the broker and delivered to anyone who subscribes later (the latest value);
- **last will**: a message the broker sends *on behalf of* the device if it vanishes. ClimaControl's is
  `availability = offline`, so the server learns immediately when the controller is gone.

### 7.11 Time on a microcontroller

`millis()` counts milliseconds since start, but it does not know the time of day. The real time comes
from the internet (**NTP**) when the farm WiFi is connected, or from the *"Set clock from this
device"* button, which sends your phone's time. After a power cut it is unknown again, so schedule rules
(the light) go to failsafe until the time is set. A battery-backed clock chip would solve this, but the
firmware does not support one yet.

---

## 8. Guided tour: one second in the life of ClimaControl

Open the files side by side as you read. The excerpts are slightly shortened.

### 8.1 Power on: `src/main.cpp`

```cpp
void setup() {
    Serial.begin(115200);
    app.begin();          // FIRST: put outputs into a safe state and start the control task

    const cc::Config cfg = app.config();
    network.begin(cfg);   // WiFi access point + farm WiFi
    web.begin();          // web server on port 80
    mqtt.begin(cfg);      // MQTT (if enabled)
    esp_task_wdt_add(nullptr);   // let the watchdog watch this loop too
}

void loop() {             // the network task
    network.loop();
    web.loop();           // answer web requests
    mqtt.loop();          // send/receive MQTT
    esp_task_wdt_reset(); // feed the watchdog
    if (app.rebootDue()) ESP.restart();
    delay(2);
}
```

Note the order: safety first, network second.

### 8.2 Starting up: `App::begin()` in `src/App.cpp`

```cpp
void App::begin() {
    mutex_ = xSemaphoreCreateMutex();   // the "key" for shared data
    loadConfig();                       // /config.json → backup → factory defaults
    outputs_.configure(config_);        // every output to its FAILSAFE level
    setenv("TZ", config_.time.tz.c_str(), 1); tzset();   // time zone
    sensors_.configure(config_);
    fusion_.configure(config_);
    controller_.configure(config_, millis());
    esp_task_wdt_init(kWatchdogS, true);                 // 15 s watchdog
    xTaskCreatePinnedToCore(controlTask, "control", 12288, this, 3, &task_, 1);
    //                      function     name       stack  arg  prio handle core
}
```

`xTaskCreatePinnedToCore` starts the control task: a function that runs in parallel from now on,
with priority 3 (more important than the network loop) on CPU core 1.

### 8.3 The heartbeat: `App::controlLoop()`

```cpp
void App::controlLoop() {
    esp_task_wdt_add(nullptr);
    cc::Config cfg = config();

    for (;;) {                                   // forever
        const uint32_t start = millis();

        // 1. pick up commands from the web/MQTT side (under the lock)
        { Lock lock(mutex_); commands.swap(pending_); /* + new config if changed */ }
        for (const Command& c : commands) {
            if (c.manual) controller_.setManual(c.output, c.level, c.durationS, start);
            else controller_.setAuto(c.output);
        }

        // 2. read sensors, 3. calibrate and fuse
        samples_ = sensors_.poll(millis(), controller_);
        cc::applyCalibration(cfg, samples_);
        fusion_.update(samples_, now);

        // 4. time of day (or -1 if the clock was never set)
        int minuteOfDay = clockValid ? local.tm_hour * 60 + local.tm_min : -1;

        // 5. decide, 6. switch
        controller_.tick(fusion_, now, minuteOfDay);
        outputs_.apply(controller_.outputs());

        // 7. publish a snapshot for web/MQTT (under the lock)
        { Lock lock(mutex_); snapshot_ = std::move(snap); }

        // 8. I'm alive; sleep until the next second (or until a command wakes us)
        esp_task_wdt_reset();
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(kTickMs - elapsed));
    }
}
```

That is the whole control system in about 30 lines. Each step is one module.

### 8.4 Step 2: reading a sensor, in `src/Sensors.cpp`

First the multiplexer is switched to the sensor's channel:

```cpp
bool Sensors::select(int channel) {
    Wire.beginTransmission(cfg_.i2c.muxAddress);         // talk to the TCA9548
    Wire.write(channel < 0 ? 0 : (1 << channel));        // one bit per channel
    return Wire.endTransmission() == 0;                  // 0 means "it answered"
}
```

Then the SCD41 is read with its raw protocol: "is data ready?" and then "send the measurement":

```cpp
Sensors::Result Sensors::readScd41(Device& d, cc::SensorSample& s) {
    uint8_t raw[9];
    uint16_t status;
    if (!i2cCommand(addr, cc::scd41::kGetDataReadyStatus)) return Result::Error;   // send 0xE4B8
    delay(2);
    if (!i2cRead(addr, raw, 3) || !cc::scd41::decodeWords(raw, 1, &status)) return Result::Error;
    if (!cc::scd41::dataReady(status)) return Result::NotReady;   // new data only every 5 s

    if (!i2cCommand(addr, cc::scd41::kReadMeasurement)) return Result::Error;      // send 0xEC05
    delay(2);
    float co2, t, h;
    if (!i2cRead(addr, raw, 9) || !cc::scd41::decodeMeasurement(raw, co2, t, h)) return Result::Error;
    s.m.set(cc::Quantity::Co2, co2);
    s.m.set(cc::Quantity::Temperature, t);
    s.m.set(cc::Quantity::Humidity, h);
    return Result::Fresh;
}
```

And the conversion from raw bytes, in `lib/core/src/Sensirion.cpp`, comes straight from the datasheet:

```cpp
bool decodeMeasurement(const uint8_t raw[9], float& co2, float& temperature, float& humidity) {
    uint16_t w[3];
    if (!decodeWords(raw, 3, w)) return false;          // any CRC wrong → reject
    co2 = w[0];                                         // ppm
    temperature = -45.0f + 175.0f * w[1] / 65535.0f;    // °C
    humidity = 100.0f * w[2] / 65535.0f;                // %RH
    return true;
}
```

If a read fails three times in a row, the sensor is re-initialised. If the multiplexer itself stops
answering three times, the whole bus is reset.

### 8.5 Step 3: fusion, in `lib/core/src/Fusion.cpp`

```cpp
for (Signal& sig : signals_) {                      // e.g. "chamber.humidity"
    values.clear();
    for (int i : sig.sensorIndexes) {               // every sensor feeding this signal
        const SensorSample& s = samples[i];
        if (!s.m.has[q] || nowMs - s.atMs > staleMs_) continue;    // missing or too old
        const float v = s.m.value[q];
        if (!(v >= range.min && v <= range.max)) continue;         // impossible value
        values.push_back(v);
    }
    sig.sources = values.size();
    sig.valid = !values.empty();
    if (!sig.valid) { sig.value = NAN; continue; }
    std::sort(values.begin(), values.end());
    const size_t n = values.size();
    sig.value = n % 2 ? values[n / 2] : (values[n / 2 - 1] + values[n / 2]) / 2;   // median
    sig.disagree = values.back() - values.front() > tolerance_[q];
}
```

### 8.6 Step 5: the decision, in `lib/core/src/Controller.cpp`

Evaluating one rule, for example the humidifier's `below` rule:

```cpp
case RuleType::Below:
    if (v <= rule.on) latch = true;          // ≤ 85 % → switch on
    else if (v >= rule.off) latch = false;   // ≥ 90 % → switch off
    return {true, latch ? rule.level : 0};   // in between: keep the last state (hysteresis)
```

Combining the rules of one output and applying safety (shortened):

```cpp
if (st.mode == OutputMode::Manual) {
    level = st.manualLevel;                       // a person decided
} else {
    bool anyInvalid = false;
    for (each rule j) {
        RuleResult r = evalRule(oc.rules[j], st.latch[j], fusion, nowMs, minuteOfDay);
        if (!r.valid) anyInvalid = true;          // rule could not decide: input missing
        else level = std::max(level, r.level);    // highest rule wins
    }
    if (anyInvalid) { level = oc.failsafe; reason = "failsafe"; }   // play it safe
}
// then: maxOnS (forced pause), minOnS/minOffS (no rapid switching)
```

### 8.7 Step 6: switching, in `src/Outputs.cpp`

```cpp
void Outputs::write(const Channel& c, float level) {
    if (c.pwm) {
        int duty = static_cast<int>(level * kPwmMax + 0.5f);   // 0..1 → 0..1023
        if (!c.activeHigh) duty = kPwmMax - duty;
        ledcWrite(c.ledc, duty);
    } else {
        digitalWrite(c.pin, (level > 0) == c.activeHigh ? HIGH : LOW);
    }
}
```

This is the same `digitalWrite` and `ledcWrite` as in the blink and fade examples of chapter 5.

### 8.8 The other direction: pressing "On" in the web page

```
 phone: button "On" for "light"
   │  JavaScript: POST /api/output  name=light&mode=on&minutes=0   (with login)
   ▼
 WebUi.cpp:   server_.on("/api/output", HTTP_POST, ...)  → checks login → app.setOutput(...)
   ▼
 App.cpp:     setOutput() checks the name, puts a Command into pending_ (under the lock),
              wakes the control task with xTaskNotifyGive(task_)
   ▼
 control task wakes up immediately, takes the command → controller_.setManual("light", 1, 3600 s)
   ▼
 controller: mode = manual, level 1 → outputs: digitalWrite(GPIO 7, HIGH) → SSR on → lamp on
   ▼
 snapshot updated → the page's next /api/status shows "light: ON, manual, 60 min left"
```

An MQTT command (`wildnis/climacontrol/cmd/output/light` = `on`) takes the same path from
`Mqtt::onMessage` onward.

### 8.9 Why `lib/core` has no Arduino code

Notice that Fusion and Controller never call `millis()`, `digitalWrite` or `Wire`. They receive the
time and the readings as parameters and return decisions. That means they can run **on your PC**,
where a test can simulate "the sensor died 31 seconds ago" in microseconds:

```cpp
void test_failsafe_when_sensors_missing() {
    Config cfg = oneSensorConfig();
    cfg.outputs.push_back(relay("fan", 16, ruleAbove("chamber.co2", 1000, 800), /*failsafe*/ 1));
    Rig rig(cfg);
    rig.co2(500, 1000);                        // CO₂ 500 ppm at t = 1 s
    TEST_ASSERT_FALSE(rig.tick(1000).on);      // low CO₂ → fan off
    const OutputState& st = rig.tick(1000 + 31000);   // 31 s later, no new readings
    TEST_ASSERT_TRUE(st.on);                   // → fan ON (failsafe)
    TEST_ASSERT_TRUE(st.failsafe);
    TEST_ASSERT_TRUE(hasAlarm(rig.ctl, "signal.chamber.co2.invalid"));
}
```

This is the main reason the code is split into two layers.

---

## 9. Hands-on exercises with the real firmware

### Exercise 1: explore the running system

With `esp32s3_sim` flashed and your phone on the `ClimaControl` WiFi at `http://192.168.4.1`:

1. **Overview:** watch `chamber.co2` slowly rise (the "mushrooms" breathe) until the fans start at 1000 ppm.
2. **Outputs:** press **On** for `uv`. The browser asks for the login (`admin` / `pilzezucht`). The *Why*
   column shows `manual` and a countdown. Press **Auto** and it goes back to `no rules`.
3. **System:** press **Set clock from this device**. The `light` output leaves failsafe and follows the
   08:00–20:00 schedule.

### Exercise 2: see the outputs as LEDs

Wire LEDs with 220 Ω resistors (as in 5.3) to GPIO 4, 5, 6, 7, 15, 16, 17, 18 = OUT1…OUT8 (hepa,
humidifier, uv, light, inlet, outlet, fan_pwm_1, fan_pwm_2). Now you see the controller switch. The
LED on GPIO 17 (`fan_pwm_1`) changes brightness with CO₂.

Or, without any wiring: build the `wokwi` environment and start the Wokwi simulator, which has these
LEDs built in (`diagram.json`). Then open `http://localhost:8180`.

### Exercise 3: change a setting without programming

In the **Configuration** tab, find the `light` output and change its window:

```json
"rules": [ { "type": "schedule", "windows": [ { "from": "07:30", "to": "19:30" } ], "level": 1 } ]
```

Press **Save**. Now try a mistake: set the light's `"pin": 0` and save. The firmware refuses with
`outputs[3].pin: GPIO 0 strapping pin, may toggle during boot` and nothing is stored. That is the
validation from `validateConfig` protecting the hardware.

### Exercise 4: see more log messages

By default the serial log shows only warnings and errors. In `platformio.ini`, change
`-DCORE_DEBUG_LEVEL=2` to `-DCORE_DEBUG_LEVEL=3`, upload again, and open the monitor. Now you also see
info messages such as the config source at boot and every output switching (`hepa -> ON (auto)`).
Change it back before building the production firmware, to keep the log readable.

### Exercise 5: run the unit tests

The tests are compiled for your PC, so they need a C++ compiler for the PC (`g++`). Windows does
not come with one, and the PlatformIO extension does not install it either. The easiest way is the
Docker wrapper. Start **Docker Desktop**, then in a terminal in the project folder:

```powershell
.\tools\pio.ps1 test -e native
```

The first run builds the Docker image and takes a few minutes. You should see one line per test ending in
`[PASSED]` and finally `native  test_core  PASSED`. No board is needed.

(On Linux or macOS, or on Windows with MinGW/MSYS2 `g++` installed, `pio test -e native` works
directly.)

### Exercise 6: write your own test

There is a test for the hysteresis of an `above` rule, but none specifically for `below`. Add this to
`test/test_core/test_core.cpp`, before `int main()`:

```cpp
void test_below_hysteresis() {
    Config cfg = oneSensorConfig();                 // two sensors in group "chamber"
    Rule r;
    r.type = RuleType::Below;
    r.signal = "chamber.humidity";
    r.on = 85;                                      // on at ≤ 85 %
    r.off = 90;                                     // off at ≥ 90 %
    cfg.outputs.push_back(relay("humidifier", 16, r));
    Rig rig(cfg);

    auto humidity = [&](float h, uint32_t t) {      // helper: set both sensors' humidity
        for (auto& s : rig.samples) {
            s = co2Sample(800, t);
            s.m.set(Quantity::Humidity, h);
        }
    };

    humidity(88, 1000);
    TEST_ASSERT_FALSE(rig.tick(1000).on);           // between thresholds, starts off
    humidity(84, 2000);
    TEST_ASSERT_TRUE(rig.tick(2000).on);            // dropped below 85 → on
    humidity(88, 3000);
    TEST_ASSERT_TRUE(rig.tick(3000).on);            // between thresholds → stays on
    humidity(91, 4000);
    TEST_ASSERT_FALSE(rig.tick(4000).on);           // reached 90 → off
}
```

and register it in `main()`:

```cpp
    RUN_TEST(test_below_hysteresis);
```

Run the tests again: your new test is listed and passes. Then break it on purpose (change `r.on = 85` to `r.on = 80`) and
watch the test fail with the line number. That is how tests protect the logic from accidental changes.

### Exercise 7: follow the code path of a feature

Pick `maxOnS` (forced humidifier pause) and find:

1. where it is read from JSON (`Config.cpp`, `readOutput`);
2. where it is validated (`validateConfig`: `cooldownS` must be > 0);
3. where it acts (`Controller::tickOutput`, the `lockedOut` logic);
4. which alarm it raises (`updateAlarms`: `output.<name>.max_on`);
5. which test covers it (`test_max_on_forces_cooldown`).

Every feature follows this path: **Config → validation → Controller → alarm → test**.

---

## 10. When something goes wrong

| Symptom | Likely cause | What to do |
|---------|--------------|------------|
| No COM port appears | charge-only cable, wrong USB port, missing driver | use a data cable and the **UART** port; check Device Manager → Ports; install the CH343 driver from the chip maker if needed |
| Upload stuck at `Connecting...` | the board did not enter download mode | hold BOOT, tap RST, release BOOT, upload again |
| `Access denied` / port busy | serial monitor or another program has the port open | close the monitor (PlatformIO normally does this itself) |
| Garbage characters in the monitor | wrong baud rate | 115200 |
| Board restarts again and again; reset reason `brownout` | power too weak | another USB port or cable, powered hub |
| Reset reason `watchdog` | a task hung for 15 s | look at the log before the restart; report it with the log |
| Reset reason `crash` | software bug | the monitor shows a decoded backtrace (function names + lines), copy it |
| WiFi `ClimaControl` does not appear | access point disabled in config, or the board is not running | serial log; if needed, reflash (the config stays) |
| The web page asks for a password | changing things needs the login | `admin` / `pilzezucht` unless changed |
| Config save shows errors | validation found a problem | the message names the exact field, e.g. `outputs[2].pin: ...` |
| Sensor shows `init failed` / many failures | wiring, address, mux channel, power | run the I²C scanner from 5.7; compare with `sensors[]` in the config |
| A signal is `null`, outputs in `failsafe` | all sensors of that signal failed | Sensors tab: which ones? Check the wiring. |
| `light` in failsafe after a power cut | clock not set | System → Set clock, or connect farm WiFi for NTP |
| Forgot the passwords | – | reflashing does **not** reset the config. A full flash erase (PlatformIO → Platform → Erase Flash) followed by upload returns to factory defaults |

---

## Glossary

| Term | Explanation |
|------|-------------|
| **Access point (AP)** | A WiFi network the ESP32 creates itself |
| **Active high / low** | Whether "on" means the pin is HIGH (normal) or LOW (inverted driver) |
| **Alarm** | A warning or critical problem shown in the UI and sent via MQTT |
| **Arduino framework** | A set of easy functions (`digitalWrite`, `Serial`, …) and the `setup`/`loop` structure |
| **Baud rate** | Speed of the serial connection (115200 bits/s here) |
| **Bootloader** | Tiny built-in program that starts the firmware or receives a new one |
| **Broker** | MQTT server that passes messages between devices |
| **Compile** | Translate source code into machine code |
| **CRC** | Checksum to detect corrupted data |
| **DevKit** | Development board: module + USB + regulator + pins |
| **Environment** | A build variant in `platformio.ini` (board + options) |
| **Failsafe level** | The output level used when the input for its rules is missing |
| **Firmware** | The program stored in the microcontroller's flash |
| **Flash** | Permanent memory on the chip (firmware, config) |
| **Flashing** | Writing firmware into flash memory |
| **FreeRTOS** | Small operating system that runs tasks in parallel |
| **GND** | Ground, 0 V reference |
| **GPIO** | A general-purpose pin that can be an input or output |
| **Heap** | RAM available for data created while running (shown as `freeHeap`) |
| **Hysteresis** | Two thresholds (on/off) to prevent rapid switching |
| **I²C** | Two-wire bus for sensors (SDA + SCL), each device has an address |
| **JSON** | Text format for structured data; used for config and status |
| **Last will** | MQTT message the broker publishes if a device disappears |
| **LEDC** | The ESP32's hardware PWM unit |
| **LittleFS** | Small, power-cut-safe file system in flash |
| **mDNS** | Lets the device be found as `climacontrol.local` |
| **Median** | Middle value of a sorted list; ignores single outliers |
| **millis()** | Milliseconds since start; wraps after 49.7 days |
| **Module** | The metal-can component with chip, flash and antenna |
| **MQTT** | Lightweight publish/subscribe messaging protocol |
| **Multiplexer (TCA9548)** | I²C switch: one input, 8 selectable channels |
| **Mutex** | Lock that lets only one task access shared data at a time |
| **NTP** | Getting the current time from the internet |
| **OTA** | Over-the-air: firmware update via WiFi |
| **PlatformIO** | Build tool / VS Code extension for microcontrollers |
| **PSRAM** | Extra external RAM on some modules (e.g. 8 MB on N8R8) |
| **Pull-up / pull-down** | Resistor holding a line HIGH / LOW when nothing drives it |
| **PWM** | Fast on/off switching to emulate an intermediate level |
| **Retained** | MQTT message kept by the broker for later subscribers |
| **Rule** | Configured condition that decides an output's level |
| **Serial monitor** | Text window showing what the firmware prints over USB |
| **Signal** | Fused value of a group, e.g. `chamber.co2` |
| **Snapshot** | Copy of the current state for UI and MQTT |
| **SSR** | Solid-state relay: switches 230 V from a 3.3 V signal |
| **Station (STA)** | The ESP32 joining an existing WiFi network |
| **Strapping pin** | Pin read at boot; can toggle during reset, so unsafe for outputs |
| **Task** | A function running in parallel under FreeRTOS |
| **Unit test** | Small automated check of one piece of logic, run on the PC |
| **Watchdog** | Timer that restarts the chip if the software stops responding |
| **Wokwi** | Online/VS Code simulator for microcontrollers |
