# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

Firmware (Arduino/C++) for a mini weather station running on a LILYGO T-ETH-Elite ESP32-S3 board. It reads indoor/outdoor climate sensors, light, rain, and gas/smoke data, exposes them over a built-in web dashboard and JSON API, and provides an authenticated remote console reachable over HTTP WebSocket, BLE (Nordic UART Service), and USB Serial.

## Build / flash

This is an Arduino sketch (`weather-station-esp32s3.ino`), not a CMake/PlatformIO project - build and upload via the Arduino IDE.

Arduino IDE -> Tools settings (also documented in the `.ino` header comment):
- Board: `ESP32S3 Dev Module`
- USB CDC On Boot: `Enabled`
- Flash Size: `16MB (128Mb)`
- Partition Scheme: `16M Flash (3MB APP/9.9MB FATFS)`
- PSRAM: `OPI PSRAM`

Required libraries (Arduino Library Manager):
- `Adafruit BME280 Library` (Adafruit)
- `Adafruit BMP280 Library` (Adafruit)
- `BH1750` (Christopher Laws)
- `WebSockets` (Markus Sattler)

There is no separate lint/test tooling - correctness is verified by compiling in the Arduino IDE and exercising the device (or its HTTP/console interfaces) directly.

## Configuration

`config.h` is gitignored and holds real secrets/pin mappings; `config.h.example` is the tracked template. When setting up a fresh checkout, copy `config.h.example` to `config.h` and fill in:
- `WIFI_SSID` / `WIFI_PASSWORD` (Ethernet is primary; WiFi is only a fallback when ETH has no link within `WIFI_FALLBACK_MS`)
- `OTA_PASSWORD_HASH` and `CONSOLE_PASSWORD_HASH` - both are SHA256 hex digests (`echo -n "yourpassword" | sha256sum`), never plaintext passwords
- Pin assignments for the W5500 Ethernet chip, two I2C buses, rain sensor, and MQ-2 gas sensor
- `STATION_ALTITUDE_M` (used to compute sea-level-adjusted pressure)

`config.h` also defines the `LOG`/`LOGLN` macros and the `SensorData` struct shared across the whole codebase - most other headers depend on it.

## Architecture

Each concern lives in its own `.cpp`/`.h` pair, wired together from `weather-station-esp32s3.ino`'s `setup()`/`loop()`. Nothing here uses `delay()` in the main loop path - everything is non-blocking, polled from `loop()`, or run on a separate FreeRTOS task - except the deliberate final 3s warning blink in `app_network.cpp`'s `restartWithWarning()` right before a forced reboot, which busy-waits by design since nothing else needs to run in that window.

- **sensors.cpp** - owns the global `sensorData` (extern `SensorData`). Two I2C buses drive the sensors: `I2C0` carries both the BH1750 lux sensor and the inside climate sensor (shared bus - addresses don't collide), `I2C1` carries the outside BME280 (required). The inside sensor is a BMP280 (temp+pressure only, no humidity - `sensorData.inHumidity` is always `NAN` and every consumer (console `status`, `/api/data` JSON, dashboard) must treat that as "not available" rather than a real 0%) and is optional/best-effort. Rain and MQ-2 gas are read via ADC + digital threshold pins. `sensorsInit()` returns false (and the device reports `LED_MODE_SENSOR_ERROR`) only if the outside BME280 or BH1750 is missing - the inside sensor never affects this.
- **app_network.cpp** - manages Ethernet (W5500 via `ETH.begin`) as the primary link with WiFi as fallback, all through the Arduino `WiFi.onEvent` state machine. Once either link comes up, `startNetworkServices()` lazily starts OTA, the HTTP webserver, and the WebSocket console exactly once. If neither link has been up for `NETWORK_RESTART_MS` (5 min), the device self-restarts.
- **app_webserver.cpp** - a single `WebServer` on port 80 serving an inlined dashboard (`/`), a JSON snapshot (`/api/data`, polled every 5s by the dashboard), and an inlined terminal-style console page (`/console`) that talks to the WebSocket console on port 81. The page markup/CSS/JS themselves live in `html_dashboard.h`/`html_console.h` as `PROGMEM` string literals, `#include`d by `app_webserver.cpp` - there's no filesystem/SPIFFS and no separate build step, it's still one `.bin` flash like the rest of the sketch, just split out of the request-handling code for readability.
- **console.cpp** - the single source of truth for console commands (`help`, `status`, `net`, `reconnect`, `reboot`, `i2c`, `gpio <pin>`, `wifiscan`, `mem`, `nvs`, `nvs reset`, `beep [Hz] [ms] [vol%]`, `melody ok|error`, `version`). `consoleHandleLine()`/`consoleAuth()` are transport-agnostic (take a `Print&` for output) and are reused identically by Serial, BLE, and WebSocket transports. Auth is a per-connection `bool authed` gate via `login <password>`, checked against a SHA256 hash - Serial is implicitly trusted and skips this gate. `consoleProcessLine()` bundles the auth-check + backlog-replay-on-login sequence shared by the BLE and WebSocket transports so it isn't duplicated (and can't silently diverge) between them.
- **ble_console.cpp** / **ws_console.cpp** - transport adapters wrapping `consoleProcessLine()` over BLE NUS and a raw WebSocket (port 81), respectively. Each tracks its own per-connection auth state (BLE: single global `bleAuthed`; WS: `authed` per client in a `std::map`). BLE serializes access to its shared output buffer/characteristic with a *recursive* mutex, since a command handled while the mutex is held can itself log (`LOGLN`), which re-enters `bleConsolePush()` on the same task.
- **logger.cpp** - `LOG`/`LOGLN` (from config.h) funnel through here. Every log line is mirrored to: Serial (if `DEBUG` defined), a 2KB ring buffer (for console backlog replay on login), BLE, and WebSocket - so log output and console command output share the same live stream on all connected clients.
- **led_status.cpp** - non-blocking blink-pattern state machine on the onboard + external LED pins, reflecting current health (`LED_MODE_OK/ERROR/SENSOR_ERROR/OTA/RESTARTING`) plus a transient "pulse" overlay fired on any request/command activity.
- **storage.cpp** - thin wrapper over `Preferences` (NVS) for persisting boot count and cumulative uptime across reboots/OTA updates.
- **buzzer.cpp** - drives the passive piezo buzzer on `BUZZER_PIN` via the LEDC PWM peripheral. `buzzerBeep(freqHz, durationMs, volumePct)` sets the tone frequency and fakes a volume knob by scaling PWM duty within the 0-50% span (50% duty is the loudest a passive piezo sees). `buzzerPlayMelody()` plays a short non-blocking note sequence (`BUZZER_MELODY_OK`/`BUZZER_MELODY_ERROR`); `weather-station-esp32s3.ino`'s `loop()` triggers these automatically whenever `ledStatusGetMode()` actually changes (entering `LED_MODE_OK` = OK chime, entering an error mode = warning beep) - not on every loop iteration, only on the transition. Everything is non-blocking: `buzzerTick()` (polled from `loop()`) advances/ends tones via `millis()` - no `delay()`.
- **timesync.cpp** - runs on its own FreeRTOS task; fetches wall-clock time once from `https://api.sefinek.net/api/v2/time` (an external service the project owner controls) to compute and cache a boot timestamp, since the ESP32 has no RTC battery. Polls for connectivity every 500ms, retries the fetch every 30s once connected until it succeeds, then goes idle. `synced`/`bootUnixMs` are `std::atomic` since they're written from this task and read from the main loop (console `status`, `/api/data`).

### Adding a new sensor
Add fields to `SensorData` in `config.h`, read/populate them in `sensorsRead()` (sensors.cpp), then surface them in three independent places that must be kept in sync manually: the `printStatus()` text in console.cpp, the JSON payload in `handleApiData()` (app_webserver.cpp), and the dashboard's `INDEX_HTML`/`FIELDS` JS object (also app_webserver.cpp).

### Adding a new console command
Add a branch in `consoleHandleLine()` (console.cpp) and a line in `printHelp()` - this alone makes it available identically over Serial, BLE, and the web console.
