#include <Arduino.h>
#include <WebSocketsServer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <map>
#include "ws_console.h"
#include "console.h"
#include "led_status.h"
#include "logger.h"
#include "config.h"

static WebSocketsServer        webSocket(81);
static std::map<uint8_t, bool> clients;
static String                  pendingOut;
static SemaphoreHandle_t       pendingMutex = xSemaphoreCreateMutex();

static void onEvent(uint8_t num, WStype_t type, uint8_t *payload, size_t length) {
    switch (type) {
        case WStype_CONNECTED: {
            clients[num] = false;
            ledStatusPulse();
            LOG("[WS] Client #%u connected\n", num);
            StringPrint out;
            consolePrintWelcome(out);
            webSocket.sendTXT(num, out.buf);
            break;
        }

        case WStype_DISCONNECTED:
            clients.erase(num);
            LOG("[WS] Client #%u disconnected\n", num);
            break;

        case WStype_TEXT: {
            const String line((const char *)payload, length);
            StringPrint out;

            bool authed = clients.count(num) ? clients[num] : false;
            consoleProcessLine(authed, line, out);
            clients[num] = authed;

            // Runs on the same task as webSocket.loop(), so sending directly
            // here (rather than via the pendingOut queue) is safe.
            if (out.buf.length()) webSocket.sendTXT(num, out.buf);
            break;
        }

        default:
            break;
    }
}

void wsConsoleInit() {
    webSocket.begin();
    webSocket.onEvent(onEvent);
    webSocket.enableHeartbeat(15000, 3000, 2);
    LOGLN("[WS] Console listening on port 81");
}

void wsConsoleHandle() {
    webSocket.loop();

    xSemaphoreTake(pendingMutex, portMAX_DELAY);
    if (pendingOut.length()) {
        for (const auto &c : clients) {
            if (c.second) webSocket.sendTXT(c.first, pendingOut);
        }
        pendingOut = "";
    }
    xSemaphoreGive(pendingMutex);
}

void wsConsolePush(const char *data, size_t len) {
    xSemaphoreTake(pendingMutex, portMAX_DELAY);
    pendingOut.concat(data, len);
    xSemaphoreGive(pendingMutex);
}
