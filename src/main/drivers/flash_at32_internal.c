/*
 * This file is part of INAV, distributed under the GNU GPL v3 or later.
 * Experimental internal Flash Blackbox backend; see docs/development/at32-internal-blackbox.md.
 */
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "platform.h"

#ifdef USE_FLASH_AT32_INTERNAL

#if !defined(AT32F43x) || !defined(AT32F435CMU7) || MCU_FLASH_SIZE != 4032
#error "Internal Blackbox requires the AT32F435CMU7 4032 KiB build"
#endif
#if defined(MSP_FIRMWARE_UPDATE) || defined(FIRMWARE_SIZE) || defined(CONFIG_IN_EXTERNAL_FLASH)
#error "Internal Blackbox cannot share its volume with firmware or configuration partitions"
#endif
#if defined(USE_FLASH_M25P16) || defined(USE_FLASH_W25N01G) || defined(USE_FLASH_W25N02K)
#error "Select only the internal Flash backend for this experimental target"
#endif

#include "drivers/flash_at32_internal.h"
#include "drivers/time.h"
#include "fc/runtime_config.h"

#define INTERNAL_FLASH_SECTOR_SIZE 4096U
#define INTERNAL_FLASH_SIZE (1984U * 1024U)
#define INTERNAL_FLASH_PAGE_SIZE 4U
#define INTERNAL_FLASH_SIZE_REGISTER 0x1FFFF7E0U
#define INTERNAL_FLASH_READY_TIMEOUT_MS 1000U
#define INTERNAL_FLASH_FLAGS (FLASH_BANK2_ODF_FLAG | FLASH_BANK2_PRGMERR_FLAG | FLASH_BANK2_EPPERR_FLAG)

extern uint8_t __flashlog_start;
extern uint8_t __flashlog_end;

static flashGeometry_t geometry;
static bool hasFailed;

static void failVolume(void)
{
    // Zero capacity makes FlashFS reach EOF and stop logging after a hardware failure.
    hasFailed = true;
    geometry.totalSize = 0;
}

static bool isRangeValid(uint32_t address, int length)
{
    return length >= 0 && address <= INTERNAL_FLASH_SIZE && (uint32_t)length <= INTERNAL_FLASH_SIZE - address;
}

bool at32InternalFlashInit(int flashNumToUse)
{
    (void)flashNumToUse;
    memset(&geometry, 0, sizeof(geometry));
    hasFailed = false;

    // Fail closed on a CGU7 or a build with a linker script that does not reserve bank 2.
    if (*(volatile const uint16_t *)INTERNAL_FLASH_SIZE_REGISTER != 4032U ||
        !FLASH->usd_bit.btopt ||
        (uintptr_t)&__flashlog_start != FLASH_BANK2_START_ADDR ||
        (uintptr_t)&__flashlog_end != FLASH_BANK2_END_ADDR + 1U) {
        return false;
    }

    geometry = (flashGeometry_t) {
        .sectors = INTERNAL_FLASH_SIZE / INTERNAL_FLASH_SECTOR_SIZE,
        .pageSize = INTERNAL_FLASH_PAGE_SIZE,
        .sectorSize = INTERNAL_FLASH_SECTOR_SIZE,
        .totalSize = INTERNAL_FLASH_SIZE,
        .pagesPerSector = INTERNAL_FLASH_SECTOR_SIZE / INTERNAL_FLASH_PAGE_SIZE,
        .flashType = FLASH_TYPE_NOR,
    };
    return at32InternalFlashWaitForReady(INTERNAL_FLASH_READY_TIMEOUT_MS);
}

bool at32InternalFlashIsReady(void)
{
    // A failed operation is finished, not perpetually busy (CLI erase polls this).
    return hasFailed || flash_bank2_operation_status_get() != FLASH_OPERATE_BUSY;
}

bool at32InternalFlashWaitForReady(timeMs_t timeoutMillis)
{
    const timeMs_t start = millis();
    const timeMs_t timeout = timeoutMillis ? timeoutMillis : INTERNAL_FLASH_READY_TIMEOUT_MS;
    while (!at32InternalFlashIsReady()) {
        if ((timeMs_t)(millis() - start) >= timeout) {
            failVolume();
            return false;
        }
    }
    return !hasFailed;
}

void at32InternalFlashEraseSector(uint32_t address)
{
    if (ARMING_FLAG(ARMED) || hasFailed || geometry.totalSize == 0 ||
        address % INTERNAL_FLASH_SECTOR_SIZE || !isRangeValid(address, INTERNAL_FLASH_SECTOR_SIZE) ||
        !at32InternalFlashWaitForReady(0)) {
        return;
    }

    flash_bank2_unlock();
    flash_flag_clear(INTERNAL_FLASH_FLAGS);
    const flash_status_type status = flash_sector_erase(FLASH_BANK2_START_ADDR + address);
    flash_bank2_lock();
    if (status != FLASH_OPERATE_DONE) {
        failVolume();
    }
}

void at32InternalFlashEraseCompletely(void)
{
    if (ARMING_FLAG(ARMED)) {
        return;
    }
    // Never call flash_erase(): that would destroy the executing firmware and configuration.
    for (uint32_t end = INTERNAL_FLASH_SIZE; end > 0 && !hasFailed; end -= INTERNAL_FLASH_SECTOR_SIZE) {
        at32InternalFlashEraseSector(end - INTERNAL_FLASH_SECTOR_SIZE);
    }
}

uint32_t at32InternalFlashPageProgram(uint32_t address, const uint8_t *data, int length)
{
    if (!data || hasFailed || geometry.totalSize == 0 || !isRangeValid(address, length) ||
        length > (int)(INTERNAL_FLASH_PAGE_SIZE - address % INTERNAL_FLASH_PAGE_SIZE) ||
        !at32InternalFlashWaitForReady(0)) {
        return address;
    }

    // Do not program a location twice: the controller requires erased destinations.
    for (int i = 0; i < length; i++) {
        if (*(volatile const uint8_t *)(uintptr_t)(FLASH_BANK2_START_ADDR + address + i) != 0xFF) {
            failVolume();
            return address;
        }
    }

    flash_bank2_unlock();
    flash_flag_clear(INTERNAL_FLASH_FLAGS);
    int written = 0;
    while (written < length) {
        const uint32_t destination = FLASH_BANK2_START_ADDR + address + written;
        const int remaining = length - written;
        flash_status_type status;
        int count;
        if (destination % 4U == 0 && remaining >= 4) {
            uint32_t word;
            memcpy(&word, data + written, sizeof(word));
            status = flash_word_program(destination, word);
            count = 4;
        } else if (destination % 2U == 0 && remaining >= 2) {
            uint16_t halfword;
            memcpy(&halfword, data + written, sizeof(halfword));
            status = flash_halfword_program(destination, halfword);
            count = 2;
        } else {
            status = flash_byte_program(destination, data[written]);
            count = 1;
        }
        if (status != FLASH_OPERATE_DONE ||
            memcmp((const void *)(uintptr_t)destination, data + written, count) != 0) {
            failVolume();
            break;
        }
        written += count;
    }
    flash_bank2_lock();
    return address + written;
}

int at32InternalFlashReadBytes(uint32_t address, uint8_t *buffer, int length)
{
    if (!buffer || geometry.totalSize == 0 || !isRangeValid(address, length) || !at32InternalFlashWaitForReady(0)) {
        return 0;
    }
    memcpy(buffer, (const void *)(uintptr_t)(FLASH_BANK2_START_ADDR + address), length);
    return length;
}

const flashGeometry_t *at32InternalFlashGetGeometry(void)
{
    return &geometry;
}
#endif
