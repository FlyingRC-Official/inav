/* Regression: external NOR/NAND bulk erase keeps its asynchronous contract. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "platform.h"
#include "drivers/flash.h"
#include "io/flashfs.h"
static uint8_t storage[8192];
static flashGeometry_t layout = { .sectors = 4, .pageSize = 256, .sectorSize = 2048, .totalSize = 8192, .pagesPerSector = 8 };
static flashPartition_t partition = { FLASH_PARTITION_TYPE_FLASHFS, 0, 3 };
static int waits, erases;
static bool ready = true;
const flashGeometry_t *flashGetGeometry(void) { return &layout; }
bool flashIsReady(void) { return ready; }
bool flashWaitForReady(timeMs_t timeout) { (void)timeout; waits++; return false; }
void flashEraseSector(uint32_t address) { assert(address < sizeof(storage)); erases++; }
void flashPartitionErase(flashPartition_t *p) { assert(p == &partition); erases++; ready = false; }
uint32_t flashPartitionSize(flashPartition_t *p) { assert(p == &partition); return sizeof(storage); }
flashPartition_t *flashPartitionFindByType(flashPartitionType_e type) { return type == FLASH_PARTITION_TYPE_FLASHFS ? &partition : NULL; }
uint32_t flashPageProgram(uint32_t address, const uint8_t *bytes, int count) { assert(address + count <= sizeof(storage)); memcpy(storage + address, bytes, count); return address + count; }
int flashReadBytes(uint32_t address, uint8_t *bytes, int count) { memcpy(bytes, storage + address, count); return count; }
void flashFlush(void) {}
#include "../../main/io/flashfs.c"
int main(void)
{
    memset(storage, 0xFF, sizeof(storage));
    flashfsInit();
    uint8_t bytes[1000];
    for (unsigned i = 0; i < sizeof(bytes); i++) { bytes[i] = (uint8_t)i; }
    flashfsWrite(bytes, sizeof(bytes), true);
    assert(tailAddress == sizeof(bytes) && memcmp(storage, bytes, sizeof(bytes)) == 0);
    ready = false;
    for (int i = 0; i < 200; i++) { flashfsWriteByte(0x42); }
    assert(flashfsTransmitBufferUsed() == 127);
    flashfsEraseCompletely();
    assert(erases == 1 && waits == 0 && tailAddress == 0 && flashfsGetWriteBufferFreeSpace() == 127);
    puts("PASS: external FlashFS synchronous write, full buffer, asynchronous bulk erase regression");
}
