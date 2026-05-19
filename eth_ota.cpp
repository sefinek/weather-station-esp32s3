#include "utilities.h"
#include <Arduino.h>
#include <ArduinoOTA.h>
#include "esp_task_wdt.h"
#if ESP_ARDUINO_VERSION < ESP_ARDUINO_VERSION_VAL(3,0,0)
  #include <ETHClass2.h>
  #define ETH ETH2
  #include <WiFi.h>
#else
  #include <ETH.h>
  #include <Network.h>
#endif
#include "config.h"
#include "logger_net.h"

static bool   eth_connected = false;
static bool   netlogger_started = false;
static bool   event_registered = false;
static String g_hostname;

static void ETH_EventHandler(arduino_event_id_t event)
{
    switch (event) {
        case ARDUINO_EVENT_ETH_START:
            if (g_hostname.length()) ETH.setHostname(g_hostname.c_str());
            LOGLN("ETH started");
            break;

        case ARDUINO_EVENT_ETH_CONNECTED:
            LOGLN("ETH connected");
            break;

        case ARDUINO_EVENT_ETH_GOT_IP: {
            if (!netlogger_started) {
                if (NetLogger::i().begin(NL_PORT)) {
                    netlogger_started = true;
                    LOGLN("NetLogger started successfully");
                    LOGF("Telnet server listening on port %d\n", NL_PORT);
                }
            }

            const String mac = ETH.macAddress();
            const String ip = ETH.localIP().toString();
            const String gw = ETH.gatewayIP().toString();
            LOGF("ETH MAC: %s; IPv4: %s", mac.c_str(), ip.c_str());
            if (ETH.fullDuplex()) LOG(", FULL_DUPLEX");
            LOGF("; %dMbps; Gateway: %s\n", ETH.linkSpeed(), gw.c_str());
            eth_connected = true;
            break;
        }

        case ARDUINO_EVENT_ETH_DISCONNECTED:
            LOGLN("ETH disconnected");
            eth_connected = false;
            break;

        case ARDUINO_EVENT_ETH_STOP:
            LOGLN("ETH stopped");
            eth_connected = false;
            break;

        default: break;
    }
}

void eth_ota_begin()
{
    LOGLN("Booting (Ethernet OTA)");
    g_hostname = String(OTA_HOSTNAME);
#if ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3,0,0)
    if (!event_registered) { Network.onEvent(ETH_EventHandler); event_registered = true; }
#else
    if (!event_registered) { WiFi.onEvent(ETH_EventHandler); event_registered = true; }
#endif

#ifdef ETH_POWER_PIN
    pinMode(ETH_POWER_PIN, OUTPUT);
    digitalWrite(ETH_POWER_PIN, HIGH);
    LOGLN("ETH power enabled");
#endif

#if CONFIG_IDF_TARGET_ESP32
    if (!ETH.begin(ETH_TYPE, ETH_ADDR, ETH_MDC_PIN, ETH_MDIO_PIN, ETH_RESET_PIN, ETH_CLK_MODE)) {
        LOGLN("ETH start failed (LAN PHY)");
        return;
    }
    LOGLN("ETH PHY initialized (LAN8720/TLK110)");
#else
    if (!ETH.begin(ETH_PHY_W5500, 1, ETH_CS_PIN, ETH_INT_PIN, ETH_RST_PIN, SPI3_HOST, ETH_SCLK_PIN, ETH_MISO_PIN, ETH_MOSI_PIN)) {
        LOGLN("ETH start failed (W5500)");
        return;
    }
    LOGLN("ETH controller initialized (W5500)");
#endif

    ArduinoOTA.setHostname(OTA_HOSTNAME);
    ArduinoOTA.setPort(OTA_PORT);

    if (!OTA_PASSWD || strlen(OTA_PASSWD) == 0) {
       LOGLN("OTA password cannot be empty");
       while (true) yield();
    }
    if (strlen(OTA_PASSWD) != 32) {
       LOGLN("OTA password must be 32-character MD5 hash");
       while (true) yield();
    }

    ArduinoOTA.setPasswordHash(OTA_PASSWD);

    ArduinoOTA
        .onStart([]() {
            String type = (ArduinoOTA.getCommand() == U_FLASH) ? "sketch" : "filesystem";
            LOGF("OTA Update started: %s\n", type.c_str());
        })
        .onEnd([]() {
            LOGLN("OTA Update complete - Rebooting...");
        })
        .onProgress([](unsigned int progress, unsigned int total) {
            const uint32_t pct = total ? (progress * 100u) / total : 0u;
            static uint32_t lastPct = 0;
            if (pct != lastPct && (pct % 10u) == 0u) {
                LOGF("%u%%|", pct);
                lastPct = pct;
            }
            if (progress == total) {
                LOGLN("OTA Progress: 100% - Complete");
            }
        })
        .onError([](ota_error_t error) {
            const char* msg = "";
            switch (error) {
                case OTA_AUTH_ERROR:    msg = "Authentication failed"; break;
                case OTA_BEGIN_ERROR:   msg = "Begin failed"; break;
                case OTA_CONNECT_ERROR: msg = "Connection failed"; break;
                case OTA_RECEIVE_ERROR: msg = "Receive failed"; break;
                case OTA_END_ERROR:     msg = "End failed"; break;
                default:                msg = "Unknown error"; break;
            }
            LOGF("OTA Error[%u]: %s\n", error, msg);
        });

    ArduinoOTA.begin();
    Serial.println("OTA service started");
}

void eth_ota_handle() {
    ArduinoOTA.handle();
    if (netlogger_started) NetLogger::i().handle();
}

bool eth_is_connected() {
    return eth_connected;
}

bool netlogger_is_running() {
    return netlogger_started && eth_connected && NetLogger::i().isConnected();
}

void netlogger_status_report() {
    LOGF("NetLogger status: Port %d, Connected: %s, Clients: %s\n",
         NL_PORT,
         eth_connected ? "YES" : "NO",
         NetLogger::i().isConnected() ? "YES" : "NO");

    if (eth_connected) {
        const String ip = ETH.localIP().toString();
        const String gw = ETH.gatewayIP().toString();
        const String dns = ETH.dnsIP().toString();
        LOGF("Network: %s, Gateway: %s, DNS: %s\n", ip.c_str(), gw.c_str(), dns.c_str());
    }
}

void system_info_report() {
    LOGLN("System Info:");
    LOGF(" Free heap: %u bytes (min: %u bytes)\n", ESP.getFreeHeap(), ESP.getMinFreeHeap());
    LOGF(" CPU freq: %u MHz\n", ESP.getCpuFreqMHz());
    LOGF(" Uptime: %lu seconds\n", millis() / 1000);
    LOGF(" Core version: %d.%d.%d\n", ESP_ARDUINO_VERSION_MAJOR, ESP_ARDUINO_VERSION_MINOR, ESP_ARDUINO_VERSION_PATCH);
}