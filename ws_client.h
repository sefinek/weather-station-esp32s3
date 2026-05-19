#pragma once
#include <Arduino.h>

bool    ws_is_ready();
bool    ws_is_connected();
void    ws_begin();
void    ws_service();
bool    ws_send_data(const char* json_data);
bool    ws_send_weather(float t, float rh, float p_hpa, float lx, int rain_raw);
bool    ws_send_as3935_event(const char* event_type, uint8_t distance = 0, uint32_t magnitude = 0);
uint8_t ws_get_state();
