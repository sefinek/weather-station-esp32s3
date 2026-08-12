#pragma once
#include <stdint.h>

enum BuzzerMelody {
    BUZZER_MELODY_OK,
    BUZZER_MELODY_ERROR,
};

void buzzerInit();
void buzzerTick();
void buzzerBeep(uint32_t freqHz, uint32_t durationMs, uint8_t volumePct = 100);
void buzzerPlayMelody(BuzzerMelody melody);
void buzzerOff();
