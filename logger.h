#pragma once
#include <Arduino.h>

void loggerPrintf(const char *fmt, ...);
void loggerPrintln(const char *msg);
uint32_t loggerSnapshot(char *out, size_t outSize, uint32_t since);
