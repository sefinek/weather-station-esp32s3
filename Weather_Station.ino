#include <Wire.h>
#include <Adafruit_BME280.h>
#include "SparkFun_AS3935.h"
#include <BH1750.h>
#include "config.h"
#include "eth_ota.h"
#include "utilities.h"
#include "timestamp.h"
#include "ws_client.h"
#include "logger_net.h"
#include "blinker.h"
#include "esp_task_wdt.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>

// I2C devices
constexpr int      SDA1_PIN = 16, SCL1_PIN = 8; // * szyna #1: BME280 + AS3935
constexpr uint8_t  BME_ADDR    = 0x76;           // Czujnik temperatury itp.
constexpr uint8_t  AS3935_ADDR = 0x03;           // Czujnik burzy
constexpr int      SDA2_PIN = 40, SCL2_PIN = 42; // * szyna #2: BH1750
constexpr uint8_t  BH1750_ADDR = 0x23;           // Czujnik światła
constexpr uint32_t I2C_FREQ    = 400000;         // I2C frequency in Hz

// AS3935
constexpr int      AS3935_IRQ    = 2;       // IRQ pin
constexpr uint8_t  NOISE_INT     = 0x01;
constexpr uint8_t  DISTURBER_INT = 0x04;
constexpr uint8_t  LIGHTNING_INT = 0x08;
SparkFun_AS3935 lightning(AS3935_ADDR);
volatile bool lightning_irq = false;
uint32_t strike_count = 0;

// Misc
constexpr int RAIN_A0 = 13;
constexpr int LED_MAIN = 15;

// Init
Adafruit_BME280 bme;
BH1750 lightMeter;

// Configuration
constexpr uint32_t PERIOD_MS = 3000;
constexpr uint32_t WS_SEND_INTERVAL_MS = 41600;

static SemaphoreHandle_t i2c1_mtx = nullptr;

static TaskHandle_t meas_task = nullptr;
static void meas_task_fn(void*) {
  TickType_t tick = xTaskGetTickCount();
  const TickType_t period = pdMS_TO_TICKS(PERIOD_MS);
  TickType_t next_send = tick + pdMS_TO_TICKS(WS_SEND_INTERVAL_MS);

  for (;;) {
    float t, rh, p, lx;
    int rain_inverted;

    lx = lightMeter.readLightLevel();

    xSemaphoreTake(i2c1_mtx, portMAX_DELAY);
    bme.takeForcedMeasurement();
    t  = bme.readTemperature();
    rh = bme.readHumidity();
    p  = bme.readPressure() * 0.01f;
    xSemaphoreGive(i2c1_mtx);

    rain_inverted = 4095 - analogRead(RAIN_A0);

    if (isfinite(lx) && isfinite(t) && isfinite(rh) && isfinite(p)) {
      const TickType_t now = xTaskGetTickCount();
      const bool ws_due = now >= next_send;
      bool ws_sent = false;
      if (ws_due) {
        next_send = now + pdMS_TO_TICKS(WS_SEND_INTERVAL_MS);
        if (ws_is_ready()) ws_sent = ws_send_weather(t, rh, p, lx, rain_inverted);
      }
      LOGF("TEMP: %.2fC  HUMID: %.2f%%  PRESS: %.2f hPa  LIGHT: %.2f lx  RAIN: %d%s\n",
           t, rh, p, lx, rain_inverted,
           ws_due ? (ws_sent ? "  [WS: SENT]" : "  [WS: SKIPPED]") : "");
    } else {
      LOGLN("Invalid sensor readings detected, skipping...");
    }

    vTaskDelayUntil(&tick, period);
  }
}

void halt(const __FlashStringHelper* msg) {
  Serial.println(msg);
  LOGLN_P((PGM_P)msg);
  blinker_start(LED_MAIN, 0, 225, true, true);
  blinker_start(LED_PIN, 0, 225, true, true);
  constexpr uint32_t LOG_EVERY_MS = 4000;
  uint32_t now = millis(), nextLog = now + LOG_EVERY_MS;
  for (;;) {
    now = millis();
    if ((int32_t)(now - nextLog) >= 0) {
      Serial.println(msg);
      LOGLN_P((PGM_P)msg);
      nextLog += LOG_EVERY_MS;
    }
    blinker_service();
    eth_ota_handle();
    yield();
  }
}

void IRAM_ATTR as3935_isr() { lightning_irq = true; }

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 1500) {}

  blinker_begin(LED_MAIN); digitalWrite(LED_MAIN, HIGH);
  blinker_begin(LED_PIN);  digitalWrite(LED_PIN, HIGH);

  // --- Ethernet & WebSocket ---
  eth_ota_begin();
  Serial.println("Ethernet & OTA initialization started");
  ws_begin();

  // I2C #1: BME280 + AS3935
  Wire.begin(SDA1_PIN, SCL1_PIN);
  Wire.setClock(I2C_FREQ);

  i2c1_mtx = xSemaphoreCreateMutex();
  if (!i2c1_mtx) halt(F("I2C1 mutex create fail"));
  if (!bme.begin(BME_ADDR)) halt(F("BME280 init fail"));

  bme.setSampling(
    Adafruit_BME280::MODE_FORCED,    //
    Adafruit_BME280::SAMPLING_X2,    // temperatura: oversampling x2 → dobra dokładność, mało szumu
    Adafruit_BME280::SAMPLING_X4,    // ciśnienie: oversampling x4 → wystarczające do meteo, szybki pomiar
    Adafruit_BME280::SAMPLING_X1,    // wilgotność: oversampling x1 → wystarczy, bo czujnik ma ±3% błąd
    Adafruit_BME280::FILTER_X4,      // filtr IIR x4 – wygładza dane, małe opóźnienie reakcji
    Adafruit_BME280::STANDBY_MS_1000 // odstęp między pomiarami: 1000 ms (1 Hz)
  );

  LOGF("I2C Bus #1 initialized (SDA:%d, SCL:%d)\n", SDA1_PIN, SCL1_PIN);

  // AS3935 init
  pinMode(AS3935_IRQ, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(AS3935_IRQ), as3935_isr, RISING);

  bool asok = lightning.begin();
  if (!asok) halt(F("AS3935 init fail"));

  xSemaphoreTake(i2c1_mtx, portMAX_DELAY);
  lightning.resetSettings();
  lightning.clearStatistics(true);        // opcjonalne wyczyszczenie statystyk z 15-minutowego okna pomiarowego
  lightning.setIndoorOutdoor(INDOOR);     // tryb zewnętrzny lub wewnętrzny
  lightning.setNoiseLevel(2);             // próg szumu tła; 0–7, większe = mniej czuły na szum; zbyt niski = INT_NH w „brudnym” RF; 2 to rozsądny default
  lightning.watchdogThreshold(5);         // próg watchdoga – minimalna energia impulsu do dalszej analizy; 0–10, większe = wyższy próg kwalifikacji
  lightning.spikeRejection(6);            // rejekcja impulsów – kształt czasowy „prawdziwej” błyskawicy; 0–15, większe = lepsze odrzucanie śmieci kosztem zasięgu
  lightning.lightningThreshold(1);        // ile uderzeń w 15 min do zgłoszenia; możliwe: 1, 5, 9, 16
  lightning.maskDisturber(false);         // true = nie zgłaszaj INT_D (fałszywek/iskrzeń urządzeń), false = zgłaszaj
  lightning.tuneCap(64);                  // opcjonalne dostrojenie anteny
  // lightning.calibrateOsc();
  xSemaphoreGive(i2c1_mtx);


  // I2C #2: BH1750
  Wire1.begin(SDA2_PIN, SCL2_PIN);
  Wire1.setClock(I2C_FREQ);
  if (!lightMeter.begin(BH1750::CONTINUOUS_HIGH_RES_MODE, BH1750_ADDR, &Wire1)) halt(F("BH1750 init fail"));

  LOGF("I2C Bus #2 initialized (SDA:%d, SCL:%d)\n", SDA2_PIN, SCL2_PIN);


  // Rain sensor
  analogReadResolution(12);

  if (xTaskCreatePinnedToCore(meas_task_fn, "meas", 6144, nullptr, 1, &meas_task, 1) != pdPASS) {
    LOGLN("meas task create failed");
  }

  blinker_start(LED_MAIN, 600, 300, false, true);
  blinker_start(LED_PIN, 600, 300, false, true);

  LOGLN("Weather Station ready!");
}

void loop() {
  static uint32_t eth_last_check = 0;
  const uint32_t now = millis();

  if ((int32_t)(now - eth_last_check) > 100) {
    const uint32_t start_time = millis();
    eth_ota_handle();
    const uint32_t duration = millis() - start_time;
    if ((int32_t)duration > 50) LOGF("WARNING: eth_ota_handle took %lu ms\n", (unsigned long)duration);
    eth_last_check = now;
  }

  blinker_service();

  // Detekcja burzy
  if (lightning_irq) {
    lightning_irq = false;

    uint8_t it;
    xSemaphoreTake(i2c1_mtx, portMAX_DELAY);
    it = lightning.readInterruptReg();
    if (it == LIGHTNING_INT) {
      ++strike_count;
      const uint8_t d = lightning.distanceToStorm();
      const uint32_t e = lightning.lightningEnergy();
      xSemaphoreGive(i2c1_mtx);

      if (ws_is_ready()) ws_send_as3935_event("lightning", d, e);
      LOGF("AS3935: Lightning strike -> Distance: %u km  Energy: %lu  Count: %lu\n", (unsigned)d, (unsigned long)e, (unsigned long)strike_count);

      blinker_start(LED_MAIN, 800, 200, false, true);
      blinker_start(LED_PIN, 800, 200, false, true);
      return;
    } else if (it == NOISE_INT) {
      xSemaphoreGive(i2c1_mtx);

      LOGLN("AS3935: Noise detected");
      if (ws_is_ready()) ws_send_as3935_event("noise");

      blinker_start(LED_PIN, 1400, 700, false, true);
      return;
    } else if (it == DISTURBER_INT) {
      xSemaphoreGive(i2c1_mtx);

      LOGLN("AS3935: Disturber detected");
      if (ws_is_ready()) ws_send_as3935_event("disturber");

      blinker_start(LED_PIN, 1200, 600, false, true);
      return;
    } else {
      xSemaphoreGive(i2c1_mtx);

      if (it != 0x00 && it != 0xFF) {
        LOGF("AS3935: Unknown interrupt 0x%02X\n", it);
        if (ws_is_ready()) ws_send_as3935_event("unknown");

        blinker_start(LED_PIN, 800, 400, false, true);
      }
    }
  }
}
