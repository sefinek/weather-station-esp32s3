#include <Arduino.h>
#include "buzzer.h"
#include "config.h"

static const uint8_t RESOLUTION_BITS = 10;                    // duty range 0-1023
static const uint32_t MAX_DUTY       = (1UL << RESOLUTION_BITS) - 1;

static bool          active   = false;
static unsigned long offAt    = 0;

struct Note { uint16_t freq; uint16_t ms; uint8_t vol; };

// Cheerful ascending major triad (B5-E6-G6) - "all good, station is up".
static const Note MELODY_OK[] = {
    {988,  90, 80},
    {1319, 90, 80},
    {1568, 160, 90},
};

// Short descending double-beep - unmistakably a warning, not a chime.
static const Note MELODY_ERROR[] = {
    {350, 150, 90},
    {250, 250, 90},
};

static const Note *melodyNotes = nullptr;
static uint8_t     melodyLen   = 0;
static uint8_t     melodyIdx   = 0;
static bool         melodyPlaying = false;

void buzzerInit() {
    ledcAttach(BUZZER_PIN, 2000, RESOLUTION_BITS);
    ledcWrite(BUZZER_PIN, 0);
}

void buzzerOff() {
    ledcWrite(BUZZER_PIN, 0);
    active = false;
    melodyPlaying = false;
}

static void startTone(uint32_t freqHz, uint32_t durationMs, uint8_t volumePct) {
    volumePct = constrain(volumePct, 0, 100);
    ledcChangeFrequency(BUZZER_PIN, freqHz, RESOLUTION_BITS);
    // 50% duty is the loudest symmetric square wave a passive piezo sees -
    // volume is faked by scaling the duty within that 0-50% span.
    ledcWrite(BUZZER_PIN, (MAX_DUTY / 2) * volumePct / 100);
    active = true;
    offAt = millis() + durationMs;
}

void buzzerBeep(uint32_t freqHz, uint32_t durationMs, uint8_t volumePct) {
    melodyPlaying = false;  // a manual one-shot beep cancels any melody in progress
    startTone(freqHz, durationMs, volumePct);
}

static void playNote(uint8_t idx) {
    melodyIdx = idx;
    startTone(melodyNotes[idx].freq, melodyNotes[idx].ms, melodyNotes[idx].vol);
}

void buzzerPlayMelody(BuzzerMelody melody) {
    switch (melody) {
        case BUZZER_MELODY_OK:
            melodyNotes = MELODY_OK;
            melodyLen   = sizeof(MELODY_OK) / sizeof(MELODY_OK[0]);
            break;
        case BUZZER_MELODY_ERROR:
            melodyNotes = MELODY_ERROR;
            melodyLen   = sizeof(MELODY_ERROR) / sizeof(MELODY_ERROR[0]);
            break;
    }
    melodyPlaying = true;
    playNote(0);
}

void buzzerTick() {
    if (!active || (long)(millis() - offAt) < 0) return;

    active = false;
    ledcWrite(BUZZER_PIN, 0);

    if (!melodyPlaying) return;
    if (melodyIdx + 1 < melodyLen) {
        playNote(melodyIdx + 1);
    } else {
        melodyPlaying = false;
    }
}
