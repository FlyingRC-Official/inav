/* Execute the production driver and FlashFS against mapped Flash and fault injection. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <unistd.h>
#include "platform.h"
#include "drivers/flash.h"
#include "drivers/flash_at32_internal.h"
#include "io/flashfs.h"

#if defined(__APPLE__)
__asm__(".globl ___flashlog_start\n.set ___flashlog_start, 0x08200000\n.globl ___flashlog_end\n.set ___flashlog_end, 0x083F0000");
#else
__asm__(".globl __flashlog_start\n.set __flashlog_start, 0x08200000\n.globl __flashlog_end\n.set __flashlog_end, 0x083F0000");
#endif

mockFlash_t mockFlash;
uint32_t armingFlags;
static uint32_t clockMs;
static bool unlocked;
static int failProgramAfter, failEraseAfter, eraseCount, programCount, shortLimit;
static bool forceBusy, corruptProgram;
static uint32_t firstErased, lastErased;

#include "../../main/drivers/flash_at32_internal.c"
#include "../../main/drivers/flash.c"
#include "../../main/io/flashfs.c"

static uint8_t *at(uint32_t address) { return (uint8_t *)(uintptr_t)address; }
timeMs_t millis(void) { return clockMs++; }
flash_status_type flash_bank2_operation_status_get(void) { return forceBusy ? FLASH_OPERATE_BUSY : FLASH_OPERATE_DONE; }
void flash_bank2_unlock(void) { assert(!unlocked); unlocked = true; }
void flash_bank2_lock(void) { assert(unlocked); unlocked = false; }
void flash_flag_clear(uint32_t flags) { assert(unlocked && flags == INTERNAL_FLASH_FLAGS); }
static flash_status_type program(uint32_t address, const void *value, size_t count)
{
    assert(unlocked && address >= FLASH_BANK2_START_ADDR && address + count <= FLASH_BANK2_END_ADDR + 1U);
    if (failProgramAfter == programCount++) { return FLASH_PROGRAM_ERROR; }
    for (size_t i = 0; i < count; i++) { assert(at(address)[i] == 0xFF); }
    if (!corruptProgram) { memcpy(at(address), value, count); }
    return FLASH_OPERATE_DONE;
}
flash_status_type flash_word_program(uint32_t address, uint32_t value) { assert(address % 4 == 0); return program(address, &value, 4); }
flash_status_type flash_halfword_program(uint32_t address, uint16_t value) { assert(address % 2 == 0); return program(address, &value, 2); }
flash_status_type flash_byte_program(uint32_t address, uint8_t value) { return program(address, &value, 1); }
flash_status_type flash_sector_erase(uint32_t address)
{
    assert(unlocked && !armingFlags && address % 4096 == 0);
    assert(address >= FLASH_BANK2_START_ADDR && address + 4096 <= FLASH_BANK2_END_ADDR + 1U);
    if (eraseCount == 0) { firstErased = address; }
    lastErased = address;
    if (failEraseAfter == eraseCount++) { return FLASH_EPP_ERROR; }
    memset(at(address), 0xFF, 4096);
    return FLASH_OPERATE_DONE;
}
static uint32_t programWithShortWrite(uint32_t address, const uint8_t *data, int count)
{
    if (shortLimit >= 0 && count > shortLimit) { count = shortLimit; shortLimit = 0; }
    return at32InternalFlashPageProgram(address, data, count);
}

static void resetVolume(void)
{
    memset(at(FLASH_BANK2_START_ADDR), 0xFF, INTERNAL_FLASH_SIZE);
    *(uint16_t *)at(INTERNAL_FLASH_SIZE_REGISTER) = 4032;
    mockFlash.usd_bit.btopt = 1;
    armingFlags = clockMs = 0;
    unlocked = forceBusy = corruptProgram = false;
    failProgramAfter = failEraseAfter = shortLimit = -1;
    programCount = eraseCount = 0;
    // Recreate the zero-initialized state of a fresh firmware boot.
    flash = NULL;
    flashPartitions = 0;
    flashPartition = NULL;
    assert(flashInit());
    assert(flashPartitionCount() == 1);
    flash->pageProgram = programWithShortWrite;
    flashfsClearBuffer();
    tailAddress = 0;
    flashfsInit();
}

int main(void)
{
    assert(mmap(at(0x08000000), 0x00400000, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON | MAP_FIXED, -1, 0) == at(0x08000000));
    assert(mmap(at(0x1FFFC000), 0x4000, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON | MAP_FIXED, -1, 0) == at(0x1FFFC000));
    memset(at(0x08000000), 0xA5, 0x200000);
    assert(mprotect(at(0x08000000), 0x200000, PROT_READ) == 0);
    resetVolume();
    assert(flashfsGetSize() == 2031616 && flashGetGeometry()->sectors == 496);

    *(uint16_t *)at(INTERNAL_FLASH_SIZE_REGISTER) = 1024;
    flash = NULL;
    flashPartitions = 0;
    assert(!flashInit() && flashGetGeometry()->totalSize == 0);
    assert(!flashIsReady() && flashPartitionCount() == 0);
    resetVolume();
    mockFlash.usd_bit.btopt = 0;
    flash = NULL;
    flashPartitions = 0;
    assert(!flashInit() && flashGetGeometry()->totalSize == 0);
    resetVolume();

    uint8_t bytes[] = {1, 2, 3, 4};
    assert(at32InternalFlashPageProgram(0, bytes, 4) == 4 && programCount == 1);
    assert(at32InternalFlashPageProgram(5, bytes, 3) == 8);
    assert(at32InternalFlashPageProgram(9, bytes, 1) == 10);
    assert(at32InternalFlashPageProgram(INTERNAL_FLASH_SIZE - 4, bytes, 4) == INTERNAL_FLASH_SIZE);
    int calls = programCount;
    assert(at32InternalFlashPageProgram(INTERNAL_FLASH_SIZE, bytes, 1) == INTERNAL_FLASH_SIZE);
    assert(at32InternalFlashPageProgram(UINT32_MAX, bytes, 4) == UINT32_MAX);
    assert(at32InternalFlashPageProgram(12, bytes, -1) == 12);
    assert(at32InternalFlashPageProgram(13, bytes, 4) == 13);
    assert(programCount == calls);
    assert(at32InternalFlashReadBytes(UINT32_MAX, bytes, 4) == 0);
    assert(at32InternalFlashPageProgram(0, bytes, 4) == 0 && flashfsIsEOF());

    resetVolume();
    corruptProgram = true;
    assert(at32InternalFlashPageProgram(0, bytes, 4) == 0 && flashfsIsEOF() && !unlocked);
    resetVolume();
    failProgramAfter = 1;
    assert(at32InternalFlashPageProgram(1, bytes, 3) == 2 && flashfsIsEOF() && !unlocked);
    resetVolume();
    forceBusy = true;
    assert(!at32InternalFlashWaitForReady(5) && flashIsReady() && flashfsIsEOF());

    resetVolume();
    for (int i = 0; i < 20; i++) { flashfsWriteByte((uint8_t)i); }
    shortLimit = 2;
    assert(!flashfsFlushAsync() && tailAddress == 2 && flashfsGetOffset() == 20);
    flashfsFlushSync();
    assert(tailAddress == 2 && flashfsGetWriteBufferFreeSpace() == 109);
    shortLimit = -1;
    flashfsFlushSync();
    assert(tailAddress == 20 && flashfsGetWriteBufferFreeSpace() == 127);
    for (int i = 0; i < 20; i++) { assert(at(FLASH_BANK2_START_ADDR)[i] == i); }

    resetVolume();
    // Short write in the first part of a wrapped ring must preserve both parts.
    bufferTail = 126;
    bufferHead = 2;
    tailAddress = 1;
    flashWriteBuffer[126] = 9;
    flashWriteBuffer[127] = 8;
    flashWriteBuffer[0] = 7;
    flashWriteBuffer[1] = 6;
    shortLimit = 1;
    assert(!flashfsFlushAsync() && tailAddress == 2 && bufferTail == 127);
    shortLimit = -1;
    flashfsFlushSync();
    assert(tailAddress == 5 && flashfsGetWriteBufferFreeSpace() == 127);
    assert(at(FLASH_BANK2_START_ADDR)[1] == 9 && at(FLASH_BANK2_START_ADDR)[2] == 8);
    assert(at(FLASH_BANK2_START_ADDR)[3] == 7 && at(FLASH_BANK2_START_ADDR)[4] == 6);

    resetVolume();
    uint8_t bulk[1000];
    for (unsigned i = 0; i < sizeof(bulk); i++) { bulk[i] = (uint8_t)(i % 251); }
    flashfsWrite(bulk, sizeof(bulk), true);
    assert(tailAddress == sizeof(bulk) && memcmp(at(FLASH_BANK2_START_ADDR), bulk, sizeof(bulk)) == 0);

    resetVolume();
    for (unsigned i = 0; i < 2000; i++) {
        if (flashfsGetWriteBufferFreeSpace() == 0) { assert(flashfsFlushAsync() || flashfsGetWriteBufferFreeSpace() > 0); }
        flashfsWriteByte((uint8_t)(i % 251));
    }
    flashfsFlushSync();
    assert(tailAddress == 2000);
    for (unsigned i = 0; i < 2000; i++) { assert(at(FLASH_BANK2_START_ADDR)[i] == i % 251); }
    flashfsInit();
    assert(tailAddress == 2048);
    uint8_t readback[4];
    assert(flashfsReadAbs(UINT32_MAX, readback, 4) == 0);
    assert(flashfsReadAbs(INTERNAL_FLASH_SIZE - 2, readback, 4) == 2);

    armingFlags = ARMED;
    calls = eraseCount;
    uint32_t offset = flashfsGetOffset();
    flashfsEraseCompletely();
    flashfsEraseRange(0, 4096);
    assert(eraseCount == calls && flashfsGetOffset() == offset);
    armingFlags = 0;
    flashfsEraseCompletely();
    assert(eraseCount == 496 && firstErased == FLASH_BANK2_END_ADDR + 1U - 4096 && lastErased == FLASH_BANK2_START_ADDR);
    assert(flashfsGetOffset() == 0 && flashfsIdentifyStartOfFreeSpace() == 0);

    for (uint32_t lastBytes = 1; lastBytes <= 3; lastBytes++) {
        resetVolume();
        flashfsSeekAbs(INTERNAL_FLASH_SIZE - lastBytes);
        for (uint32_t i = 0; i < lastBytes; i++) { flashfsWriteByte((uint8_t)i); }
        flashfsFlushSync();
        assert(tailAddress == INTERNAL_FLASH_SIZE && flashfsIsEOF());
    }
    resetVolume();
    flashfsSeekAbs(INTERNAL_FLASH_SIZE - 4);
    for (int i = 0; i < 8; i++) { flashfsWriteByte((uint8_t)i); }
    flashfsFlushSync();
    assert(flashfsIsEOF() && tailAddress == INTERNAL_FLASH_SIZE && flashfsGetWriteBufferFreeSpace() == 127);
    resetVolume();
    flashfsSeekAbs(2048);
    failEraseAfter = 1;
    flashfsEraseCompletely();
    assert(eraseCount == 2 && tailAddress == 2048 && flashfsIsEOF() && !unlocked);
    assert(at(0x08000000)[0] == 0xA5 && at(0x081FFFFF)[0] == 0xA5);
    puts("PASS: internal driver, capacity/boot guards, bounded writes, faults, ring wrap, short writes, EOF, reboot scan, erase guards/order, firmware isolation");
    return 0;
}
