/*
 * Mini Weather Station
 * Board  : LILYGO T-ETH-Elite ESP32-S3
 *
 * Required libraries (Arduino Library Manager):
 *   - "Adafruit BME280 Library"  by Adafruit
 *   - "Adafruit BMP280 Library"  by Adafruit
 *   - "BH1750"                   by Christopher Laws
 *   - "WebSockets"               by Markus Sattler
 *
 * Arduino IDE -> Tools:
 *   Board            : ESP32S3 Dev Module
 *   USB CDC On Boot  : Enabled
 *   Flash Size       : 16MB (128Mb)
 *   Partition Scheme : 16M Flash (3MB APP/9.9MB FATFS)
 *   PSRAM            : OPI PSRAM
 */

#include "config.h"
#include "sensors.h"
#include "app_network.h"
#include "app_webserver.h"
#include "led_status.h"
#include "storage.h"
#include "console.h"
#include "ble_console.h"
#include "ws_console.h"
#include "timesync.h"
#include "buzzer.h"

void setup() {
#ifdef DEBUG
    Serial.begin(115200);
    LOGLN("=== Mini Weather Station ===");
#endif

    ledStatusInit();
    storageInit();
    buzzerInit();

    if (!sensorsInit()) {
        LOGLN("[WARN] One or more sensors failed to initialize");
    }
    networkInit();
    bleConsoleInit();
    timeSyncInit();
}

void loop() {
    networkHandle();
    webserverHandle();
    wsConsoleHandle();
    bleConsoleTick();
    storageTickUptime();
    consoleTick();
    buzzerTick();
#ifdef DEBUG
    consoleHandleSerial();
#endif

    LedMode newLedMode;
    if (sensorsHaveError()) {
        newLedMode = LED_MODE_SENSOR_ERROR;
    } else if (!networkConnected()) {
        newLedMode = LED_MODE_ERROR;
    } else {
        newLedMode = LED_MODE_OK;
    }
    if (newLedMode != ledStatusGetMode()) {
        if (newLedMode == LED_MODE_OK) {
            buzzerPlayMelody(BUZZER_MELODY_OK);
        } else {
            buzzerPlayMelody(BUZZER_MELODY_ERROR);
        }
    }
    ledStatusSet(newLedMode);
    ledStatusHandle();

    static unsigned long lastRead = 0;
    const unsigned long now = millis();
    if (now - lastRead < SENSOR_INTERVAL_MS) return;
    lastRead = now;

    sensorsRead();
}
