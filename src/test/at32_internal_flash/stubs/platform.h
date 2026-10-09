#pragma once
#include <stdbool.h>
#include <stdint.h>
#define USE_FLASHFS
#define ARRAYLEN(x) (sizeof(x) / sizeof((x)[0]))
#ifdef USE_FLASH_AT32_INTERNAL
#define AT32F43x
#define AT32F435CMU7
#define MCU_FLASH_SIZE 4032
#define FLASH_BANK2_START_ADDR 0x08200000U
#define FLASH_BANK2_END_ADDR 0x083EFFFFU
#define FLASH_BANK2_ODF_FLAG 0x10000020U
#define FLASH_BANK2_PRGMERR_FLAG 0x10000004U
#define FLASH_BANK2_EPPERR_FLAG 0x10000010U
typedef enum { FLASH_OPERATE_BUSY, FLASH_PROGRAM_ERROR, FLASH_EPP_ERROR, FLASH_OPERATE_DONE, FLASH_OPERATE_TIMEOUT } flash_status_type;
typedef struct { struct { uint32_t btopt; } usd_bit; } mockFlash_t;
extern mockFlash_t mockFlash;
#define FLASH (&mockFlash)
flash_status_type flash_bank2_operation_status_get(void);
void flash_bank2_unlock(void);
void flash_bank2_lock(void);
void flash_flag_clear(uint32_t flags);
flash_status_type flash_sector_erase(uint32_t address);
flash_status_type flash_word_program(uint32_t address, uint32_t value);
flash_status_type flash_halfword_program(uint32_t address, uint16_t value);
flash_status_type flash_byte_program(uint32_t address, uint8_t value);
#endif
