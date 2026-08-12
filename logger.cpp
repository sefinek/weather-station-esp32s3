#include <Arduino.h>
#include <stdarg.h>
#include <string.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include "logger.h"
#include "config.h"
#include "ble_console.h"
#include "ws_console.h"

static const size_t RING_SIZE = 2048;
static char     ringBuf[RING_SIZE];
static uint32_t ringWritten = 0;
static SemaphoreHandle_t ringMutex = xSemaphoreCreateMutex();

static void broadcast(const char *data, size_t len) {
#ifdef DEBUG
    Serial.write((const uint8_t *)data, len);
#endif
    xSemaphoreTake(ringMutex, portMAX_DELAY);
    for (size_t i = 0; i < len; i++) {
        ringBuf[ringWritten % RING_SIZE] = data[i];
        ringWritten++;
    }
    xSemaphoreGive(ringMutex);
    bleConsolePush(data, len);
    wsConsolePush(data, len);
}

static int writeTimestamp(char *buf, size_t size) {
    const unsigned long ms = millis();
    const int n = snprintf(buf, size, "[%6lu.%03lu] ", ms / 1000UL, ms % 1000UL);
    return (n > 0 && (size_t)n < size) ? n : 0;
}

void loggerPrintf(const char *fmt, ...) {
    char buf[256];
    const int prefixLen = writeTimestamp(buf, sizeof(buf));

    va_list args;
    va_start(args, fmt);
    const int n = vsnprintf(buf + prefixLen, sizeof(buf) - prefixLen, fmt, args);
    va_end(args);
    if (n <= 0) return;

    int total = prefixLen + n;
    if (total > (int)sizeof(buf) - 1) total = (int)sizeof(buf) - 1;

    if (buf[total - 1] == '\r') {
        broadcast(buf + prefixLen, (size_t)(total - prefixLen));
        return;
    }

    broadcast(buf, (size_t)total);
}

void loggerPrintln(const char *msg) {
    char buf[256];
    const int prefixLen = writeTimestamp(buf, sizeof(buf));
    const int n = snprintf(buf + prefixLen, sizeof(buf) - prefixLen, "%s", msg);
    if (n <= 0) return;

    int total = prefixLen + n;
    if (total > (int)sizeof(buf) - 1) total = (int)sizeof(buf) - 1;
    broadcast(buf, (size_t)total);
    broadcast("\n", 1);
}

uint32_t loggerSnapshot(char *out, size_t outSize, uint32_t since) {
    xSemaphoreTake(ringMutex, portMAX_DELAY);
    const uint32_t total = ringWritten;
    const uint32_t backlog = (total < RING_SIZE) ? total : RING_SIZE;
    const uint32_t oldestAvailable = total - backlog;
    if (since < oldestAvailable) since = oldestAvailable;

    uint32_t count = total - since;
    if (count > outSize) count = (uint32_t)outSize;

    for (uint32_t i = 0; i < count; i++) {
        out[i] = ringBuf[(since + i) % RING_SIZE];
    }
    xSemaphoreGive(ringMutex);
    return since + count;
}
