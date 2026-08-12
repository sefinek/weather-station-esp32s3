#include <Arduino.h>
#include <WiFi.h>
#include <ETH.h>
#include <ArduinoOTA.h>
#include "app_network.h"
#include "config.h"
#include "app_webserver.h"
#include "led_status.h"
#include "ws_console.h"

static bool          ethConnected      = false;
static bool          wifiConnected     = false;
static bool          servicesStarted   = false;
static bool          otaConfigured     = false;
static unsigned long lastWifiAttempt   = 0;
static unsigned long disconnectedSince = 0;

static const unsigned long WIFI_RETRY_MS      = 15000;
static const unsigned long NETWORK_RESTART_MS = 300000;

static void startNetworkServices() {
    if (servicesStarted) return;
    servicesStarted = true;
    if (otaConfigured) ArduinoOTA.begin();
    webserverBegin();
    wsConsoleInit();
}

static void onNetworkEvent(arduino_event_id_t event) {
    switch (event) {
        case ARDUINO_EVENT_ETH_START:
            ETH.setHostname(STATION_HOSTNAME);
            LOGLN("[ETH] Started");
            break;
        case ARDUINO_EVENT_ETH_CONNECTED:
            LOGLN("[ETH] Connected");
            break;
        case ARDUINO_EVENT_ETH_GOT_IP:
            LOG("[ETH] IP: %s  MAC: %s  %dMbps\n",
                ETH.localIP().toString().c_str(),
                ETH.macAddress().c_str(),
                ETH.linkSpeed());
            ethConnected = true;
            startNetworkServices();
            break;
        case ARDUINO_EVENT_ETH_DISCONNECTED:
            LOGLN("[ETH] Disconnected");
            ethConnected = false;
            break;
        case ARDUINO_EVENT_ETH_STOP:
            LOGLN("[ETH] Stopped");
            ethConnected = false;
            break;
        case ARDUINO_EVENT_WIFI_STA_GOT_IP:
            LOG("[WiFi] IP: %s  RSSI: %ddBm\n",
                WiFi.localIP().toString().c_str(), WiFi.RSSI());
            wifiConnected = true;
            startNetworkServices();
            break;
        case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
            LOGLN("[WiFi] Disconnected");
            wifiConnected = false;
            break;
        default:
            break;
    }
}

void networkInit() {
    WiFi.onEvent(onNetworkEvent);
    WiFi.setAutoReconnect(false);

    ArduinoOTA.setHostname(STATION_HOSTNAME);
    if (strlen(OTA_PASSWORD_HASH) != 64) {
        LOGLN("[ERROR] OTA_PASSWORD_HASH must be a 64-character SHA256 hash - OTA disabled");
    } else {
        ArduinoOTA.setPasswordHash(OTA_PASSWORD_HASH);
        ArduinoOTA.onStart([]() {
            LOGLN("[OTA] Starting update...");
            ledStatusSet(LED_MODE_OTA);
        });
        ArduinoOTA.onEnd([]() {
            LOGLN("[OTA] Done. Rebooting...");
        });
        ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
            LOG("[OTA] %u%%\r", total ? (progress * 100u) / total : 0u);
        });
        ArduinoOTA.onError([](ota_error_t error) {
            const char *reason = "Unknown";
            switch (error) {
                case OTA_AUTH_ERROR:    reason = "Auth failed";       break;
                case OTA_BEGIN_ERROR:   reason = "Begin failed";      break;
                case OTA_CONNECT_ERROR: reason = "Connect failed";    break;
                case OTA_RECEIVE_ERROR: reason = "Receive failed";    break;
                case OTA_END_ERROR:     reason = "End failed";        break;
            }
            LOG("[OTA] Error [%u]: %s\n", error, reason);
        });
        otaConfigured = true;
    }

    if (!ETH.begin(ETH_PHY_W5500, ETH_PHY_ADDR,
                   ETH_CS_PIN, ETH_INT_PIN, ETH_RST_PIN,
                   SPI3_HOST,
                   ETH_SCLK_PIN, ETH_MISO_PIN, ETH_MOSI_PIN)) {
        LOGLN("[ERROR] ETH start failed!");
    }
}

static void restartWithWarning() {
    LOGLN("[SYS] No network for too long, restarting...");
    ledStatusSet(LED_MODE_RESTARTING);
    const unsigned long warnUntil = millis() + 3000;
    while (millis() < warnUntil) {
        ledStatusHandle();
        delay(5);
    }
    ESP.restart();
}

void networkHandle() {
    const unsigned long now = millis();

    if (ethConnected || wifiConnected) {
        disconnectedSince = 0;
    } else if (disconnectedSince == 0) {
        disconnectedSince = now;
    } else if (now - disconnectedSince >= NETWORK_RESTART_MS) {
        restartWithWarning();
    }

    if (!ethConnected && !wifiConnected && now >= WIFI_FALLBACK_MS &&
        (lastWifiAttempt == 0 || now - lastWifiAttempt >= WIFI_RETRY_MS)) {
        lastWifiAttempt = now;
        LOGLN("[WiFi] Connecting...");
        WiFi.mode(WIFI_STA);
        WiFi.setHostname(STATION_HOSTNAME);
        WiFi.setSleep(false);
        WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    }
    if (ethConnected || wifiConnected) ArduinoOTA.handle();
}

bool networkConnected() {
    return ethConnected || wifiConnected;
}

bool networkIsEthernet() {
    return ethConnected;
}

void networkForceWifiReconnect() {
    WiFi.disconnect();
    wifiConnected = false;
    lastWifiAttempt = 0;
}
