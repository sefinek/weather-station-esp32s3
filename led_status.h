#pragma once
#include <stdint.h>

enum LedMode {
    LED_MODE_OK,
    LED_MODE_ERROR,
    LED_MODE_SENSOR_ERROR,
    LED_MODE_OTA,
    LED_MODE_RESTARTING
};

void ledStatusInit();
void ledStatusSet(LedMode mode);
LedMode ledStatusGetMode();
void ledStatusHandle();
void ledStatusPulse(uint16_t ms = 250);
