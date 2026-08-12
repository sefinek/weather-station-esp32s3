#include "storage.h"
#include "config.h"
#include <Preferences.h>
#include <nvs.h>

static Preferences   prefs;
static uint32_t      bootCount   = 0;
static uint32_t      savedUptime = 0;
static unsigned long lastSave    = 0;

static const unsigned long SAVE_INTERVAL_MS = 600000UL;

void storageInit() {
    prefs.begin("ws", false);
    bootCount   = prefs.getUInt("boots", 0) + 1;
    savedUptime = prefs.getUInt("uptime", 0);
    prefs.putUInt("boots", bootCount);
    prefs.end();
    LOG("[STORAGE] Boot #%u, prior cumulative uptime %us\n", bootCount, savedUptime);
}

uint32_t storageGetBootCount() {
    return bootCount;
}

uint32_t storageGetCumulativeUptime() {
    return savedUptime + (uint32_t)(millis() / 1000UL);
}

void storageTickUptime() {
    if (millis() - lastSave < SAVE_INTERVAL_MS) return;
    lastSave = millis();
    prefs.begin("ws", false);
    prefs.putUInt("uptime", storageGetCumulativeUptime());
    prefs.end();
}

void storagePrintInfo(Print &out) {
    out.printf("Namespace \"ws\": boots=%u  savedUptime=%us  (live cumulative %us)\n",
        bootCount, savedUptime, storageGetCumulativeUptime());

    nvs_stats_t stats;
    if (nvs_get_stats(NULL, &stats) == ESP_OK) {
        out.printf("NVS: %u/%u entries used (%u free, %u namespaces)\n",
            stats.used_entries, stats.total_entries, stats.free_entries, stats.namespace_count);
    } else {
        out.println(F("NVS stats unavailable"));
    }
}

void storageReset() {
    prefs.begin("ws", false);
    prefs.clear();
    prefs.end();
    LOGLN("[STORAGE] NVS namespace \"ws\" cleared via console");
}
