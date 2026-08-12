#pragma once
#include <stdint.h>
#include <Print.h>

void     storageInit();
uint32_t storageGetBootCount();
uint32_t storageGetCumulativeUptime();
void     storageTickUptime();
void     storagePrintInfo(Print &out);
void     storageReset();
