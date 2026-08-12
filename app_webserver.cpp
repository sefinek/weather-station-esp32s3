#include <Arduino.h>
#include "app_webserver.h"
#include "app_network.h"
#include "led_status.h"
#include "sensors.h"
#include "storage.h"
#include "timesync.h"
#include <WebServer.h>
#include <ETH.h>
#include <WiFi.h>
#include <esp_system.h>

static WebServer server(80);

#include "html_dashboard.h"
#include "html_console.h"

static void handleRoot() {
    ledStatusPulse();
    server.send_P(200, "text/html", INDEX_HTML);
}

static void handleConsolePage() {
    ledStatusPulse();
    server.send_P(200, "text/html", CONSOLE_HTML);
}

static void handleApiData() {
    ledStatusPulse(200);
    char buf[1200];
    const String bootTimeStr = timeSyncBootTimeIso();
    const bool ethUp = networkIsEthernet();
    const IPAddress ip  = ethUp ? ETH.localIP()    : WiFi.localIP();
    const IPAddress gw  = ethUp ? ETH.gatewayIP()  : WiFi.gatewayIP();
    const IPAddress sn  = ethUp ? ETH.subnetMask() : WiFi.subnetMask();
    const IPAddress dns = ethUp ? ETH.dnsIP()      : WiFi.dnsIP();
    const String mac    = ethUp ? ETH.macAddress() : WiFi.macAddress();

    char inHumidityBuf[16];
    if (isnan(sensorData.inHumidity)) {
        snprintf(inHumidityBuf, sizeof(inHumidityBuf), "null");  // BMP280 inside has no humidity sensor
    } else {
        snprintf(inHumidityBuf, sizeof(inHumidityBuf), "%.2f", sensorData.inHumidity);
    }

    const int len = snprintf(buf, sizeof(buf),
        "{\"outside\":{\"temp\":%.2f,\"humidity\":%.2f,\"pressure\":%.2f,\"pressureSL\":%.2f},"
        "\"inside\":{\"temp\":%.2f,\"humidity\":%s,\"pressure\":%.2f},"
        "\"light\":{\"lux\":%.2f},"
        "\"rain\":{\"detected\":%s,\"intensity\":%d,\"raw\":%d},"
        "\"gas\":{\"detected\":%s,\"intensity\":%d,\"raw\":%d,\"ready\":%s},"
        "\"system\":{\"espTemp\":%.1f,\"heap\":%u,\"minHeap\":%u,\"maxAllocHeap\":%u,"
        "\"freePsram\":%u,\"psramSize\":%u,\"cpuFreq\":%u,\"flashSize\":%u,"
        "\"sketchSize\":%u,\"freeSketchSpace\":%u,\"chipRev\":%u,\"resetReason\":%d,"
        "\"uptime\":%lu,\"bootCount\":%u,\"cumulativeUptime\":%lu,\"sensorErrors\":%u,\"bootTime\":\"%s\"},"
        "\"network\":{\"type\":\"%s\",\"ip\":\"%s\",\"speed\":%d,\"fullDuplex\":%s,\"rssi\":%d,"
        "\"gateway\":\"%s\",\"subnet\":\"%s\",\"dns\":\"%s\",\"mac\":\"%s\"}}",
        sensorData.outTemp, sensorData.outHumidity, sensorData.outPressure, sensorData.outPressureSL,
        sensorData.inTemp, inHumidityBuf, sensorData.inPressure,
        sensorData.lux,
        sensorData.rainDetected ? "true" : "false", sensorData.rainPct, sensorData.rainRaw,
        sensorData.gasDetected ? "true" : "false", sensorData.mq2Pct, sensorData.mq2Raw,
        sensorData.mq2Ready ? "true" : "false",
        temperatureRead(),
        ESP.getFreeHeap(), ESP.getMinFreeHeap(), ESP.getMaxAllocHeap(),
        ESP.getFreePsram(), ESP.getPsramSize(),
        ESP.getCpuFreqMHz(), ESP.getFlashChipSize(),
        ESP.getSketchSize(), ESP.getFreeSketchSpace(),
        ESP.getChipRevision(), (int)esp_reset_reason(),
        millis() / 1000UL,
        storageGetBootCount(), storageGetCumulativeUptime(), sensorsGetErrorCount(),
        bootTimeStr.c_str(),
        ethUp ? "ethernet" : "wifi",
        ip.toString().c_str(),
        ethUp ? ETH.linkSpeed() : 0,
        (ethUp && ETH.fullDuplex()) ? "true" : "false",
        ethUp ? 0 : WiFi.RSSI(),
        gw.toString().c_str(),
        sn.toString().c_str(),
        dns.toString().c_str(),
        mac.c_str()
    );
    if (len <= 0 || len >= (int)sizeof(buf)) {
        server.send(500, "text/plain", "JSON buffer overflow");
        return;
    }
    server.send(200, "application/json", buf);
}

void webserverBegin() {
    static bool started = false;
    if (started) return;
    started = true;
    server.on("/", HTTP_GET, handleRoot);
    server.on("/api/data", HTTP_GET, handleApiData);
    server.on("/console", HTTP_GET, handleConsolePage);
    server.onNotFound([]() {
        ledStatusPulse();
        server.send(404, "text/plain", "Not found");
    });
    server.begin();
    LOG("[WWW] http://%s\n", (networkIsEthernet() ? ETH.localIP() : WiFi.localIP()).toString().c_str());
}

void webserverHandle() {
    server.handleClient();
}
