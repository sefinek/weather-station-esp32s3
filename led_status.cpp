#include <Arduino.h>
#include "led_status.h"
#include "config.h"

static LedMode       currentMode  = LED_MODE_OK;
static uint8_t       step         = 0;
static unsigned long lastChange   = 0;
static uint8_t       patternState = HIGH;

static bool           pulseActive   = false;
static unsigned long  pulseStart    = 0;
static uint16_t       pulseDuration = 0;

static const uint16_t PATTERN_DOUBLE[]  = {150, 100, 150, 900};
static const uint16_t PATTERN_TRIPLE[]  = {150, 100, 150, 100, 150, 900};
static const uint16_t PATTERN_OTA[]     = {100, 100};
static const uint16_t PATTERN_RESTART[] = {50, 50};

static const uint8_t STEPS_DOUBLE  = sizeof(PATTERN_DOUBLE)  / sizeof(PATTERN_DOUBLE[0]);
static const uint8_t STEPS_TRIPLE  = sizeof(PATTERN_TRIPLE)  / sizeof(PATTERN_TRIPLE[0]);
static const uint8_t STEPS_OTA     = sizeof(PATTERN_OTA)     / sizeof(PATTERN_OTA[0]);
static const uint8_t STEPS_RESTART = sizeof(PATTERN_RESTART) / sizeof(PATTERN_RESTART[0]);

static void ledWrite(uint8_t state) {
    digitalWrite(LED_STATUS_PIN,   state);
    digitalWrite(LED_EXTERNAL_PIN, state);
}

void ledStatusInit() {
    pinMode(LED_STATUS_PIN,   OUTPUT);
    pinMode(LED_EXTERNAL_PIN, OUTPUT);
    patternState = HIGH;
    ledWrite(patternState);
}

void ledStatusSet(LedMode mode) {
    if (mode == currentMode) return;
    currentMode = mode;
    step = 0;
    lastChange = millis();
    patternState = HIGH;
    if (!pulseActive) ledWrite(patternState);
}

LedMode ledStatusGetMode() {
    return currentMode;
}

void ledStatusPulse(uint16_t ms) {
    pulseActive = true;
    pulseStart = millis();
    pulseDuration = ms;
}

void ledStatusHandle() {
    if (pulseActive) {
        if (millis() - pulseStart < pulseDuration) {
            ledWrite(!patternState);
            return;
        }
        pulseActive = false;
        ledWrite(patternState);
    }

    if (currentMode == LED_MODE_OK) return;

    const uint16_t* pattern;
    uint8_t steps;
    switch (currentMode) {
        case LED_MODE_ERROR:        pattern = PATTERN_DOUBLE;  steps = STEPS_DOUBLE;  break;
        case LED_MODE_SENSOR_ERROR: pattern = PATTERN_TRIPLE;  steps = STEPS_TRIPLE;  break;
        case LED_MODE_RESTARTING:   pattern = PATTERN_RESTART; steps = STEPS_RESTART; break;
        default:                    pattern = PATTERN_OTA;     steps = STEPS_OTA;     break;
    }

    if (millis() - lastChange < pattern[step]) return;
    lastChange += pattern[step];
    step = (step + 1) % steps;
    patternState = (step % 2 == 0) ? HIGH : LOW;
    ledWrite(patternState);
}
