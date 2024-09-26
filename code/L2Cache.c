#include "L2Cache.h"

uint8_t L2Cache[L2_SIZE];
Cache2 L2;


void initCacheL2() {
    for (int i = 0; i < (L2_SIZE / BLOCK_SIZE); i++) {
        L2.line[i].Valid = 0;
        L2.line[i].Dirty = 0;
        L2.line[i].Tag = 0;
    }
    L2.init = 1;
}


void accessL2(uint32_t address, uint8_t *data, uint32_t mode) {
    uint32_t index, Tag, MemAddress;
    uint8_t TempBlock[BLOCK_SIZE];

    Tag = address >> 15; // Use the most significant 17 bits to determine the Tag (32 - 6 - 9)
    index = (address >> 6) & 0x1FF; // Use the next 9 bits to determine the index (512 lines, 0x1FF = 511)

    MemAddress = address & ~(BLOCK_SIZE - 1); // Memory address of the block

    /* init cache */
    if (L2.init == 0) {
        initCacheL2();
    }

    CacheLine *Line = &L2.line[index];

    /* access Cache */
    if (!Line->Valid || Line->Tag != Tag) {
        // if block not present - miss
        accessDRAM(MemAddress, TempBlock, MODE_READ); // get new block from DRAM

        if ((Line->Valid) && (Line->Dirty)) {
            MemAddress = (Line->Tag << 6) | (index << 6); // get address of the block in memory
            accessDRAM(MemAddress, &L2Cache[index * BLOCK_SIZE], MODE_WRITE); // then write back old block
        }

        memcpy(&L2Cache[index * BLOCK_SIZE], TempBlock, BLOCK_SIZE); // copy new block to cache line
        Line->Valid = 1;
        Line->Tag = Tag;
        Line->Dirty = 0;
    } // if miss, then replaced with the correct block

    if (mode == MODE_READ) {
        // read data from cache line
        memcpy(data, &L2Cache[index * BLOCK_SIZE + (address & (BLOCK_SIZE - 1))], WORD_SIZE);
    }
    if (mode == MODE_WRITE) {
        // write data to cache line
        memcpy(&L2Cache[index * BLOCK_SIZE + (address & (BLOCK_SIZE - 1))], data, WORD_SIZE);
        Line->Dirty = 1;
    }
}
