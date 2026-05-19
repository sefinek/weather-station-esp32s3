#pragma once
#include <Arduino.h>

void eth_ota_begin();
void eth_ota_handle();
bool eth_is_connected();
bool netlogger_is_running();
void netlogger_status_report();
void system_info_report();
