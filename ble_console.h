#pragma once
#include <stddef.h>

void bleConsoleInit();
void bleConsoleTick();
void bleConsolePush(const char *data, size_t len);
