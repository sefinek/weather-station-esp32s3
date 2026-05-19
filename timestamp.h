#pragma once
#include <Arduino.h>

void     timestamp_sync_from_unix_ms(uint64_t unix_ms);
uint64_t timestamp_now_unix_ms();
uint64_t timestamp_now_unix();
bool     timestamp_is_synced();
