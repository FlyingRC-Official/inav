#pragma once
#include <stdint.h>
extern uint32_t armingFlags;
#define ARMED 1U
#define ARMING_FLAG(flag) (armingFlags & (flag))
