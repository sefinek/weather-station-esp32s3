#include "blinker.h"

#define MAX_LEDS 4
static uint8_t led_pins[MAX_LEDS];
static BlinkState states[MAX_LEDS];
static uint8_t led_count = 0;

void blinker_begin(uint8_t led_pin) {
    if (led_count >= MAX_LEDS) return;
    led_pins[led_count] = led_pin;
    pinMode(led_pin, OUTPUT);
    digitalWrite(led_pin, LOW);
    states[led_count] = {false, 0, 0, false, 0, false, false};
    led_count++;
}

void blinker_start(uint8_t led_pin, uint16_t blink_time, uint16_t blink_delay, bool start_on, bool end_on) {
    for (uint8_t i = 0; i < led_count; i++) {
        if (led_pins[i] == led_pin) {
            unsigned long now = millis();
            states[i].active = true;
            states[i].end_time = (blink_time == 0) ? 0 : now + blink_time;
            states[i].next_toggle = now;
            states[i].state = start_on;
            states[i].delay_ms = blink_delay;
            states[i].start_on = start_on;
            states[i].end_on = end_on;
            digitalWrite(led_pin, start_on ? HIGH : LOW);
            break;
        }
    }
}

void blinker_service() {
    unsigned long now = millis();
    for (uint8_t i = 0; i < led_count; i++) {
        BlinkState &b = states[i];
        uint8_t pin = led_pins[i];
        if (b.active) {
            if (b.end_time != 0 && (long)(now - b.end_time) >= 0) {
                b.active = false;
                digitalWrite(pin, b.end_on ? HIGH : LOW);
                continue;
            }
            if ((long)(now - b.next_toggle) >= 0) {
                b.state = !b.state;
                digitalWrite(pin, b.state ? HIGH : LOW);
                b.next_toggle = now + b.delay_ms;
            }
        }
    }
}
