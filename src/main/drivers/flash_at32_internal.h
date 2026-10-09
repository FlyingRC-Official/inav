/* Internal NOR backend for the opt-in AT32F435xM Blackbox build. */
#pragma once

#include "drivers/flash.h"

bool at32InternalFlashInit(int flashNumToUse);
bool at32InternalFlashIsReady(void);
bool at32InternalFlashWaitForReady(timeMs_t timeoutMillis);
void at32InternalFlashEraseSector(uint32_t address);
void at32InternalFlashEraseCompletely(void);
uint32_t at32InternalFlashPageProgram(uint32_t address, const uint8_t *data, int length);
int at32InternalFlashReadBytes(uint32_t address, uint8_t *buffer, int length);
const flashGeometry_t *at32InternalFlashGetGeometry(void);
