#include "timestamp.h"
#include "logger_net.h"
#include <limits.h>

static uint64_t ts_base_unix_ms = 0;
static uint32_t ts_base_millis  = 0;
static bool     ts_synced       = false;

void timestamp_sync_from_unix_ms(uint64_t unix_ms) {
    if (unix_ms == 0) {
        LOGLN("Invalid timeSync value");
        return;
    }
    ts_base_unix_ms = unix_ms;
    ts_base_millis  = millis();
    ts_synced       = true;
    LOGF("Sync complete: %llu\n", (unsigned long long)unix_ms);
}

uint64_t timestamp_now_unix_ms() {
    if (!ts_synced) return 0;
    uint32_t now = millis();
    uint32_t elapsed = (now >= ts_base_millis) ? (now - ts_base_millis) : (UINT32_MAX - ts_base_millis + now + 1);
    return ts_base_unix_ms + elapsed;
}

uint64_t timestamp_now_unix() { return timestamp_now_unix_ms() / 1000ULL; }
bool     timestamp_is_synced() { return ts_synced; }
