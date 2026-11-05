# ClimaControl documentation

Firmware for the ESP32 climate controller of the Wildnismanufaktur mushroom farm. It reads the climate
sensors, switches fans, humidifier, light and UV through solid-state relays, and can be watched and
controlled through its own WiFi web page and over MQTT.

## Where to start

<div class="grid cards" markdown>

-   **[1 – Overview](01-overview.md)**

    What the system does, all hardware and software parts, how they fit together, the safety
    concept and the current status. **Start here.**

-   **[2 – Building blocks](02-building-blocks.md)**

    Every module in detail, plus reference tables: configuration keys, HTTP API, MQTT topics,
    status JSON and alarm codes.

-   **[3 – Beginner guide](03-beginner-guide.md)**

    Never touched a microcontroller? Basics, first programs to try, a C++ crash course and a guided
    tour through the ClimaControl code.

</div>

## Quick links

| I want to… | Go to |
|------------|-------|
| flash the firmware onto a board | [Beginner guide 4.4](03-beginner-guide.md#44-flashing-climacontrol-onto-your-board) |
| connect to the controller for the first time | [Overview 5](01-overview.md#5-ways-to-talk-to-the-controller) |
| change a setpoint or a schedule | [Configuration keys](02-building-blocks.md#20-reference-configuration-keys) |
| understand why an output is on or off | [Reason strings](02-building-blocks.md#21-reference-status-json-alarm-codes-reason-strings) |
| connect a server via MQTT | [MQTT topics](02-building-blocks.md#16-mqtt) |
| use the HTTP API from a script | [HTTP API](02-building-blocks.md#151-http-api) |
| know what happens when a sensor fails | [Safety concept](01-overview.md#6-safety-and-redundancy-concept) |
| fix a problem | [Troubleshooting](03-beginner-guide.md#10-when-something-goes-wrong) |
| look up a term | [Glossary](03-beginner-guide.md#glossary) |

## Updating this website

The pages are the Markdown files in `docs/`, and the menu is set in `mkdocs.yml`. Edit them in
VS Code, then rebuild. Docker Desktop must be running.

- **Double-click `tools\docs.cmd`**: builds the website into `site\` and opens it in the browser.
  The result works offline, including search, and can be copied or zipped and passed on.
- **`tools\docs.cmd serve`** in a terminal: live preview at <http://localhost:8000> that reloads
  on every save. Stop it with Ctrl+C.

The build checks every link between the pages and fails if one is broken.
