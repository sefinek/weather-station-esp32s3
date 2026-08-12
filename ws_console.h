#pragma once
#include <stddef.h>

void wsConsoleInit();
void wsConsoleHandle();
void wsConsolePush(const char *data, size_t len);
