#pragma once
#include "config.h"
#include <Print.h>

extern SensorData sensorData;

bool     sensorsInit();
void     sensorsRead();
bool     sensorsHaveError();
uint32_t sensorsGetErrorCount();
void     sensorsI2cScan(Print &out);
