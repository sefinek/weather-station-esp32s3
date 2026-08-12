#include <Arduino.h>
#include "sensors.h"
#include <Wire.h>
#include <Adafruit_BME280.h>
#include <Adafruit_BMP280.h>
#include <BH1750.h>

static TwoWire I2C0(0);
static TwoWire I2C1(1);
static Adafruit_BME280 bmeOut;
static Adafruit_BMP280 bmpIn(&I2C0);
static BH1750 lightMeter;

SensorData sensorData;

static const float SEA_LEVEL_FACTOR = powf(1.0f - STATION_ALTITUDE_M / 44330.0f, 5.255f);

static bool     bmeOutOk    = false;
static bool     bmpInOk     = false;
static bool     lightOk     = false;
static uint32_t sensorErrors = 0;

static void logRainChange(bool detected) {
    static int8_t prev = -1;
    if (prev == (int8_t)detected) return;
    LOG("[RAIN] %s\n", prev < 0
        ? (detected ? "Initial state: RAINING" : "Initial state: DRY")
        : (detected ? "Rain started" : "Rain stopped"));
    prev = detected;
}

static void logGasChange(bool detected) {
    static int8_t prev = -1;
    if (prev == (int8_t)detected) return;
    LOG("[GAS] %s\n", prev < 0
        ? (detected ? "Initial state: DETECTED" : "Initial state: CLEAN")
        : (detected ? "Gas/smoke detected!" : "Gas/smoke cleared"));
    prev = detected;
}

bool sensorsInit() {
    pinMode(RAIN_DO_PIN, INPUT_PULLUP);
    pinMode(MQ2_DO_PIN, INPUT_PULLUP);
    analogReadResolution(12);

    I2C0.begin(BH1750_SDA, BH1750_SCL, 400000);
    lightOk = lightMeter.begin(BH1750::CONTINUOUS_HIGH_RES_MODE_2, BH1750_ADDR, &I2C0);
    if (!lightOk) {
        LOGLN("[ERROR] BH1750 not found! Check wiring.");
    } else {
        LOGLN("[OK] BH1750");
    }

    // Inside sensor shares the BH1750 bus (I2C0) - its address (0x76/0x77)
    // doesn't collide with BH1750's (0x23/0x5C). Optional/best-effort: a
    // missing BMP280 doesn't count as a sensor error.
    bmpInOk = bmpIn.begin(BME280_ADDR);
    if (bmpInOk) {
        bmpIn.setSampling(
            Adafruit_BMP280::MODE_FORCED,
            Adafruit_BMP280::SAMPLING_X1,
            Adafruit_BMP280::SAMPLING_X1,
            Adafruit_BMP280::FILTER_OFF,
            Adafruit_BMP280::STANDBY_MS_1
        );
        LOGLN("[OK] BMP280 inside (no humidity)");
    } else {
        LOGLN("[ERROR] BMP280 inside not found! Check wiring.");
    }

    I2C1.begin(BME_OUT_SDA, BME_OUT_SCL, 400000);
    bmeOutOk = bmeOut.begin(BME280_ADDR, &I2C1);
    if (!bmeOutOk) {
        LOGLN("[ERROR] BME280 outside not found! Check wiring.");
    } else {
        bmeOut.setSampling(
            Adafruit_BME280::MODE_FORCED,
            Adafruit_BME280::SAMPLING_X1,
            Adafruit_BME280::SAMPLING_X1,
            Adafruit_BME280::SAMPLING_X1,
            Adafruit_BME280::FILTER_OFF
        );
        LOGLN("[OK] BME280 outside");
    }

    return bmeOutOk && lightOk;
}

bool sensorsHaveError() {
    return !(bmeOutOk && lightOk);
}

uint32_t sensorsGetErrorCount() {
    return sensorErrors;
}

void sensorsRead() {
    if (bmeOutOk) {
        bmeOut.takeForcedMeasurement();
        const float outT = bmeOut.readTemperature();
        const float outRH = bmeOut.readHumidity();
        const float outP = bmeOut.readPressure() / 100.0f;
        if (isfinite(outT) && isfinite(outRH) && isfinite(outP)) {
            sensorData.outTemp = outT;
            sensorData.outHumidity = outRH;
            sensorData.outPressure = outP;
            sensorData.outPressureSL = outP / SEA_LEVEL_FACTOR;
        } else {
            LOGLN("[ERROR] BME280 outside: invalid reading");
            sensorErrors++;
        }
    }

    if (bmpInOk) {
        bmpIn.takeForcedMeasurement();
        const float inT = bmpIn.readTemperature();
        const float inP = bmpIn.readPressure() / 100.0f;
        if (isfinite(inT) && isfinite(inP)) {
            sensorData.inTemp = inT;
            sensorData.inHumidity = NAN;  // BMP280 has no humidity sensor
            sensorData.inPressure = inP;
        } else {
            LOGLN("[ERROR] BMP280 inside: invalid reading");
            sensorErrors++;
        }
    }

    if (lightOk) {
        const float lux = lightMeter.readLightLevel();
        if (lux >= 0.0f) {
            sensorData.lux = lux;
        } else {
            LOGLN("[ERROR] BH1750: invalid reading");
            sensorErrors++;
        }
    }

    sensorData.rainRaw     = analogRead(RAIN_AO_PIN);
    sensorData.rainDetected = (digitalRead(RAIN_DO_PIN) == LOW);
    sensorData.rainPct     = constrain(map(sensorData.rainRaw, 4095, 0, 0, 100), 0, 100);
    logRainChange(sensorData.rainDetected);

    const bool mq2WasReady = sensorData.mq2Ready;
    sensorData.mq2Ready = (millis() >= MQ2_WARMUP_MS);
    if (sensorData.mq2Ready) {
        if (!mq2WasReady) LOGLN("[GAS] MQ-2 warmup complete, readings active");
        sensorData.mq2Raw     = analogRead(MQ2_AO_PIN);
        sensorData.gasDetected = (digitalRead(MQ2_DO_PIN) == LOW);
        sensorData.mq2Pct     = constrain(map(sensorData.mq2Raw, 0, 4095, 0, 100), 0, 100);
        logGasChange(sensorData.gasDetected);
    }
}

void sensorsI2cScan(Print &out) {
    TwoWire *buses[]  = { &I2C0, &I2C1 };
    const char *names[] = { "I2C0 (BH1750 + BMP280 inside bus)", "I2C1 (BME280 outside bus)" };

    for (int b = 0; b < 2; b++) {
        out.printf("%s:\n", names[b]);
        int found = 0;
        for (uint8_t addr = 1; addr < 127; addr++) {
            buses[b]->beginTransmission(addr);
            if (buses[b]->endTransmission() == 0) {
                out.printf("  0x%02X\n", addr);
                found++;
            }
        }
        if (!found) out.println(F("  (nothing found)"));
    }
}
