#include <Arduino.h>
#include <HTTPClient.h>
#if ESP_ARDUINO_VERSION < ESP_ARDUINO_VERSION_VAL(3, 0, 0)
#include <WiFiClientSecure.h>
#else
#include <NetworkClientSecure.h>
#endif
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <atomic>
#include "timesync.h"
#include "config.h"
#include "app_network.h"

static std::atomic<bool>     synced{false};
static std::atomic<uint64_t> bootUnixMs{0};

static const unsigned long RETRY_MS       = 30000;
static const unsigned long POLL_MS        = 500;
static const unsigned long SYNCED_IDLE_MS = 3600000UL;
static const char *TIME_API_URL = "https://api.sefinek.net/api/v2/time";

static bool fetchTime() {
#if ESP_ARDUINO_VERSION < ESP_ARDUINO_VERSION_VAL(3, 0, 0)
    WiFiClientSecure client;
#else
    NetworkClientSecure client;
#endif
    client.setInsecure();

    HTTPClient http;
    if (!http.begin(client, TIME_API_URL)) return false;
    http.setTimeout(5000);

    const int code = http.GET();
    if (code != 200) {
        LOG("[TIME] Request failed, HTTP %d\n", code);
        http.end();
        return false;
    }

    const String payload = http.getString();
    http.end();

    const int idx = payload.indexOf("\"unixTime\":\"");
    if (idx < 0) {
        LOGLN("[TIME] unixTime field not found in response");
        return false;
    }
    const int start = idx + 12;
    const int end = payload.indexOf('"', start);
    if (end < 0) return false;

    const uint64_t serverMs = strtoull(payload.substring(start, end).c_str(), nullptr, 10);
    if (serverMs == 0) return false;

    bootUnixMs.store(serverMs - (uint64_t)millis(), std::memory_order_relaxed);
    synced.store(true, std::memory_order_release);
    LOG("[TIME] Synced - boot time: %s\n", timeSyncBootTimeIso().c_str());
    return true;
}

static void timeSyncTask(void *) {
    for (;;) {
        if (synced.load(std::memory_order_acquire)) {
            vTaskDelay(pdMS_TO_TICKS(SYNCED_IDLE_MS));
            continue;
        }
        if (!networkConnected()) {
            vTaskDelay(pdMS_TO_TICKS(POLL_MS));
            continue;
        }
        fetchTime();
        vTaskDelay(pdMS_TO_TICKS(RETRY_MS));
    }
}

void timeSyncInit() {
    xTaskCreate(timeSyncTask, "timesync", 12288, nullptr, 1, nullptr);
}

bool timeSyncHasBootTime() {
    return synced.load(std::memory_order_acquire);
}

String timeSyncBootTimeIso() {
    if (!synced.load(std::memory_order_acquire)) return String("");
    const time_t sec = (time_t)(bootUnixMs.load(std::memory_order_relaxed) / 1000ULL);
    struct tm tmInfo;
    gmtime_r(&sec, &tmInfo);
    char buf[24];
    strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tmInfo);
    return String(buf);
}
