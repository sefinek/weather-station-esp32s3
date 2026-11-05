#include <Arduino.h>
#include "console.h"
#include "config.h"
#include "led_status.h"
#include "logger.h"
#include "sensors.h"
#include "app_network.h"
#include "storage.h"
#include "timesync.h"
#include "buzzer.h"
#include <ETH.h>
#include <WiFi.h>
#include <mbedtls/sha256.h>

static unsigned long restartAt = 0;

void consolePrintWelcome(Print &out) {
    out.println(F("=== Stacja pogodowa: Wałcz ==="));
    out.println(F("Wymagane logowanie. Użyj: login <hasło>"));
}

static void printHelp(Print &out) {
    out.println(F("Commands:"));
    out.println(F("  login <password> - authenticate (required on HTTP/BLE)"));
    out.println(F("  help              - show this list"));
    out.println(F("  status            - sensor readings"));
    out.println(F("  net               - network status"));
    out.println(F("  reconnect         - force a WiFi reconnect attempt"));
    out.println(F("  reboot            - reboot the device"));
    out.println(F("  i2c               - scan both I2C buses"));
    out.println(F("  gpio <pin>        - digital read of a pin"));
    out.println(F("  wifiscan          - list nearby WiFi networks"));
    out.println(F("  mem               - free heap/PSRAM"));
    out.println(F("  nvs               - flash storage (NVS) stats and saved values"));
    out.println(F("  nvs reset         - erase saved boot count/uptime (takes effect after restart)"));
    out.println(F("  beep [Hz] [ms] [vol%] - test the buzzer (defaults 7000Hz 500ms 100%)"));
    out.println(F("  melody ok|error   - play the boot-OK/error alert melody"));
    out.println(F("  version           - firmware build timestamp"));
}

static bool wifiScanRunning = false;

static void startWifiScan(Print &out) {
    if (wifiScanRunning) {
        out.println(F("Scan already in progress."));
        return;
    }
    if (WiFi.scanNetworks(true) == WIFI_SCAN_RUNNING) {
        wifiScanRunning = true;
        out.println(F("Scanning... (result will appear in the log)"));
    } else {
        out.println(F("Failed to start scan."));
    }
}

static void wifiScanTick() {
    if (!wifiScanRunning) return;
    const int16_t n = WiFi.scanComplete();
    if (n == WIFI_SCAN_RUNNING) return;

    wifiScanRunning = false;
    if (n == WIFI_SCAN_FAILED || n <= 0) {
        LOGLN("[WIFI] No networks found");
    } else {
        for (int i = 0; i < n; i++) {
            LOG("%-32s %4ddBm  %s\n",
                WiFi.SSID(i).c_str(), WiFi.RSSI(i),
                WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? "OPEN" : "SECURED");
        }
    }
    WiFi.scanDelete();
}

static void printMem(Print &out) {
    out.printf("Free heap: %u KB  (min %u KB)\n",
        ESP.getFreeHeap() / 1024, ESP.getMinFreeHeap() / 1024);
    out.printf("Free PSRAM: %u KB / %u KB\n",
        ESP.getFreePsram() / 1024, ESP.getPsramSize() / 1024);
}

static void printStatus(Print &out) {
    out.printf("OUT   %.1fC  %.1f%%  %.1fhPa (SL %.1fhPa)\n",
        sensorData.outTemp, sensorData.outHumidity,
        sensorData.outPressure, sensorData.outPressureSL);
    if (isnan(sensorData.inHumidity)) {
        out.printf("IN    %.1fC  n/a%%  %.1fhPa\n",
            sensorData.inTemp, sensorData.inPressure);
    } else {
        out.printf("IN    %.1fC  %.1f%%  %.1fhPa\n",
            sensorData.inTemp, sensorData.inHumidity, sensorData.inPressure);
    }
    out.printf("LIGHT %.1flx\n", sensorData.lux);
    out.printf("RAIN  %s  %d%% (ADC %d)\n",
        sensorData.rainDetected ? "RAINING" : "DRY",
        sensorData.rainPct, sensorData.rainRaw);
    if (sensorData.mq2Ready) {
        out.printf("GAS   %s  %d%% (ADC %d)\n",
            sensorData.gasDetected ? "DETECTED" : "CLEAN",
            sensorData.mq2Pct, sensorData.mq2Raw);
    } else {
        out.println(F("GAS   warming up"));
    }
    out.printf("Uptime %lus  Boots %u  Cumulative %us  Sensor errors %u\n",
        millis() / 1000UL, storageGetBootCount(),
        storageGetCumulativeUptime(), sensorsGetErrorCount());
    if (timeSyncHasBootTime()) {
        out.printf("Boot time %s\n", timeSyncBootTimeIso().c_str());
    } else {
        out.println(F("Boot time not synced yet"));
    }
}

static void printNet(Print &out) {
    if (networkIsEthernet()) {
        out.printf("ETH   %s  MAC %s  %dMbps\n",
            ETH.localIP().toString().c_str(),
            ETH.macAddress().c_str(), ETH.linkSpeed());
    } else if (networkConnected()) {
        out.printf("WiFi  %s  MAC %s  RSSI %ddBm\n",
            WiFi.localIP().toString().c_str(),
            WiFi.macAddress().c_str(), WiFi.RSSI());
    } else {
        out.println(F("Not connected"));
    }
}

static String sha256Hex(const String &input) {
    unsigned char hash[32];
    mbedtls_sha256_context ctx;
    mbedtls_sha256_init(&ctx);
    mbedtls_sha256_starts(&ctx, 0);
    mbedtls_sha256_update(&ctx, (const unsigned char *)input.c_str(), input.length());
    mbedtls_sha256_finish(&ctx, hash);
    mbedtls_sha256_free(&ctx);

    char hex[65];
    for (int i = 0; i < 32; i++) sprintf(hex + i * 2, "%02x", hash[i]);
    hex[64] = '\0';
    return String(hex);
}

bool consoleAuth(bool &authed, const String &lineIn, Print &out) {
    String line = lineIn;
    line.trim();

    if (line.startsWith("login ")) {
        if (strlen(CONSOLE_PASSWORD_HASH) != 64) {
            out.println(F("Console password hash misconfigured - login disabled."));
            LOGLN("[AUTH] Console login blocked: CONSOLE_PASSWORD_HASH misconfigured");
            return true;
        }
        if (sha256Hex(line.substring(6)) == CONSOLE_PASSWORD_HASH) {
            authed = true;
            out.println(F("OK, authenticated."));
            LOGLN("[AUTH] Console login OK");
        } else {
            out.println(F("Wrong password."));
            LOGLN("[AUTH] Console login FAILED");
        }
        return true;
    }

    if (!authed) {
        out.println(F("Not authenticated. Use: login <password>"));
        return true;
    }

    return false;
}

void consoleHandleLine(const String &lineIn, Print &out) {
    String line = lineIn;
    line.trim();
    if (line.length() == 0) return;

    ledStatusPulse();

    if (line == "help") {
        printHelp(out);
    } else if (line == "status") {
        printStatus(out);
    } else if (line == "net") {
        printNet(out);
    } else if (line == "reconnect") {
        out.println(F("Forcing WiFi reconnect..."));
        LOGLN("[SYS] WiFi reconnect requested via console");
        networkForceWifiReconnect();
    } else if (line == "reboot") {
        LOGLN("[SYS] Reboot requested via console");
        restartAt = millis() + 1000;
    } else if (line == "i2c") {
        sensorsI2cScan(out);
    } else if (line.startsWith("gpio ")) {
        const String arg = line.substring(5);
        if (arg.length() == 0) {
            out.println(F("Usage: gpio <pin>"));
        } else {
            const int pin = arg.toInt();
            if (pin < 0 || pin > 63 || !digitalPinIsValid(pin)) {
                out.printf("Invalid pin: %d\n", pin);
            } else {
                out.printf("GPIO%d = %s\n", pin, digitalRead(pin) == HIGH ? "HIGH" : "LOW");
            }
        }
    } else if (line == "wifiscan") {
        startWifiScan(out);
    } else if (line == "mem") {
        printMem(out);
    } else if (line == "nvs") {
        storagePrintInfo(out);
    } else if (line == "nvs reset") {
        out.println(F("NVS namespace cleared. Restart for boot count/uptime to reset."));
        LOGLN("[SYS] NVS reset requested via console");
        storageReset();
    } else if (line == "beep" || line.startsWith("beep ")) {
        unsigned int freq = 7000, ms = 500, vol = 100;
        sscanf(line.c_str(), "beep %u %u %u", &freq, &ms, &vol);
        if (ms > 5000) ms = 5000;  // guard against accidentally jamming the buzzer on
        buzzerBeep(freq, ms, (uint8_t)constrain((int)vol, 0, 100));
        out.printf("Beep %uHz %ums vol %u%%\n", freq, ms, vol);
    } else if (line == "melody ok") {
        buzzerPlayMelody(BUZZER_MELODY_OK);
        out.println(F("Playing OK melody"));
    } else if (line == "melody error") {
        buzzerPlayMelody(BUZZER_MELODY_ERROR);
        out.println(F("Playing error melody"));
    } else if (line == "version") {
        out.printf("Built %s %s\n", __DATE__, __TIME__);
    } else {
        out.print(F("Unknown command: "));
        out.println(line);
        out.println(F("Type 'help' for a list."));
    }
}

static const size_t SERIAL_LINE_MAX = 256;

void consoleHandleSerial() {
    static String buf;
    while (Serial.available()) {
        const char c = (char)Serial.read();
        if (c == '\n' || c == '\r') {
            if (buf.length()) {
                consoleHandleLine(buf, Serial);
                buf = "";
            }
        } else {
            buf += c;
            if (buf.length() > SERIAL_LINE_MAX) buf = "";
        }
    }
}

void consoleProcessLine(bool &authed, const String &line, Print &out) {
    const bool wasAuthed = authed;
    if (!consoleAuth(authed, line, out)) {
        consoleHandleLine(line, out);
        return;
    }
    if (authed && !wasAuthed) {
        char backlog[512];
        const uint32_t n = loggerSnapshot(backlog, sizeof(backlog), 0);
        if (n > 0) out.write((const uint8_t *)backlog, n);
    }
}

void consoleTick() {
    if (restartAt && (long)(millis() - restartAt) >= 0) {
        LOGLN("[SYS] Rebooting now");
        ESP.restart();
    }
    wifiScanTick();
}
