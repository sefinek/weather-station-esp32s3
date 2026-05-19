#pragma once
#include <Arduino.h>

struct BlinkState {
    bool active;
    unsigned long end_time;
    unsigned long next_toggle;
    bool state;
    uint16_t delay_ms;
    bool start_on;
    bool end_on;
};

void blinker_begin(uint8_t led_pin);
void blinker_start(uint8_t led_pin, uint16_t blink_time, uint16_t blink_delay, bool start_on = false, bool end_on = false);
void blinker_service();
