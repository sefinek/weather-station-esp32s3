#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include "ble_console.h"
#include "console.h"
#include "config.h"
#include "led_status.h"
#include "logger.h"

#define NUS_SERVICE_UUID "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
#define NUS_RX_UUID      "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"
#define NUS_TX_UUID      "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"

static const size_t       BLE_CHUNK_SIZE = 180;
static const size_t       BLE_LINE_MAX   = 256;
static const unsigned long BLE_WELCOME_DELAY_MS = 300;
static const uint16_t     NIMBLE_NOTIFY_BIT = 0x0001;

static BLECharacteristic *txChar = nullptr;
static bool               bleConnected = false;
static bool               bleAuthed = false;
static String             rxBuffer;
static SemaphoreHandle_t  bleMutex = xSemaphoreCreateRecursiveMutex();
static bool               welcomePending = false;
static unsigned long      welcomeAtMs = 0;

class BleOutput : public Print {
public:
    String buf;

    using Print::write;
    size_t write(uint8_t c) override {
        buf += (char)c;
        if (c == '\n' || buf.length() >= BLE_CHUNK_SIZE) flush();
        return 1;
    }

    void flush() {
        if (buf.length() && txChar && bleConnected) {
            txChar->setValue((uint8_t *)buf.c_str(), buf.length());
            txChar->notify();
        }
        buf = "";
    }
};

static BleOutput bleOut;

class TxCallbacks : public BLECharacteristicCallbacks {
    void onSubscribe(BLECharacteristic *characteristic, ble_gap_conn_desc *desc, uint16_t subValue) override {
        if (!(subValue & NIMBLE_NOTIFY_BIT)) return;
        welcomeAtMs = millis() + BLE_WELCOME_DELAY_MS;
        welcomePending = true;
    }
};

class ServerCallbacks : public BLEServerCallbacks {
    void onConnect(BLEServer *server) override {
        bleConnected = true;
        ledStatusPulse();
        LOGLN("[BLE] Client connected");
    }
    void onDisconnect(BLEServer *server) override {
        bleConnected = false;
        bleAuthed = false;
        welcomePending = false;
        LOGLN("[BLE] Client disconnected");
        BLEDevice::startAdvertising();
    }
};

class RxCallbacks : public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic *characteristic) override {
        const String value = characteristic->getValue();
        for (size_t i = 0; i < value.length(); i++) {
            const char c = value[i];
            if (c == '\n' || c == '\r') {
                if (rxBuffer.length()) {
                    xSemaphoreTakeRecursive(bleMutex, portMAX_DELAY);
                    consoleProcessLine(bleAuthed, rxBuffer, bleOut);
                    bleOut.flush();
                    xSemaphoreGiveRecursive(bleMutex);
                    rxBuffer = "";
                }
            } else {
                rxBuffer += c;
                if (rxBuffer.length() > BLE_LINE_MAX) rxBuffer = "";
            }
        }
    }
};

void bleConsoleTick() {
    if (!welcomePending || (long)(millis() - welcomeAtMs) < 0) return;
    welcomePending = false;
    if (!bleConnected || !txChar) return;
    xSemaphoreTakeRecursive(bleMutex, portMAX_DELAY);
    consolePrintWelcome(bleOut);
    bleOut.flush();
    xSemaphoreGiveRecursive(bleMutex);
}

void bleConsolePush(const char *data, size_t len) {
    if (!bleConnected || !bleAuthed || !txChar) return;
    xSemaphoreTakeRecursive(bleMutex, portMAX_DELAY);
    for (size_t i = 0; i < len; i++) bleOut.write((uint8_t)data[i]);
    bleOut.flush();
    xSemaphoreGiveRecursive(bleMutex);
}

void bleConsoleInit() {
    BLEDevice::init(STATION_HOSTNAME);

    BLEServer *server = BLEDevice::createServer();
    server->setCallbacks(new ServerCallbacks());

    BLEService *service = server->createService(NUS_SERVICE_UUID);

    txChar = service->createCharacteristic(NUS_TX_UUID, BLECharacteristic::PROPERTY_NOTIFY);
    txChar->setCallbacks(new TxCallbacks());

    BLECharacteristic *rxChar = service->createCharacteristic(
        NUS_RX_UUID,
        BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR
    );
    rxChar->setCallbacks(new RxCallbacks());

    service->start();
    BLEDevice::getAdvertising()->addServiceUUID(NUS_SERVICE_UUID);
    BLEDevice::startAdvertising();

    LOG("[BLE] Advertising as \"%s\"\n", STATION_HOSTNAME);
}
