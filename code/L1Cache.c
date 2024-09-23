#include "L1Cache.h"

uint8_t L1Cache[L1_SIZE];
uint8_t L2Cache[L2_SIZE];
uint8_t DRAM[DRAM_SIZE];
uint32_t time;
Cache L1;

/**************** Time Manipulation ***************/
void resetTime() { time = 0; }

uint32_t getTime() { return time; }

/****************  RAM memory (byte addressable) ***************/
void accessDRAM(uint32_t address, uint8_t *data, uint32_t mode) {
    if (address >= DRAM_SIZE - WORD_SIZE + 1)
        exit(-1);

    if (mode == MODE_READ) {
        memcpy(data, &(DRAM[address]), BLOCK_SIZE);
        time += DRAM_READ_TIME;
    }

    if (mode == MODE_WRITE) {
        memcpy(&(DRAM[address]), data, BLOCK_SIZE);
        time += DRAM_WRITE_TIME;
    }
}

/*********************** L1 cache *************************/

void initCache() { L1.init = 0; }

void accessL1(uint32_t address, uint8_t *data, uint32_t mode) {
    uint32_t index, Tag, MemAddress;
    uint8_t TempBlock[BLOCK_SIZE];

    Tag = address >> 3; // Why do I do this?
    index = address & 0x7;

    //printf("address -> %u ; index -> %u\n", address, index);

    MemAddress = address >> 3; // again this....!
    MemAddress = MemAddress << 3; // address of the block in memory

    /* init cache */
    if (L1.init == 0) {
        L1.line[index].Valid = 0;
        L1.init = 1;
    }

    CacheLine *Line = &L1.line[index];

    /* access Cache*/

    if (!Line->Valid || Line->Tag != Tag) {
        // if block not present - miss
        accessDRAM(MemAddress, TempBlock, MODE_READ); // get new block from DRAM

        if ((Line->Valid) && (Line->Dirty)) {
            // line has dirty block
            MemAddress = Line->Tag << 3; // get address of the block in memory
            accessDRAM(MemAddress, &(L1Cache[index]), MODE_WRITE); // then write back old block
        }

        memcpy(&(L1Cache[index]), TempBlock,
               BLOCK_SIZE); // copy new block to cache line
        Line->Valid = 1;
        Line->Tag = Tag;
        Line->Dirty = 0;
    } // if miss, then replaced with the correct block

    if (mode == MODE_READ) {
        // read data from cache line
        if (0 == (address % 8)) {
            // even word on block
            memcpy(data, &(L1Cache[index]), WORD_SIZE);
        } else {
            // odd word on block
            //buff = data & 0x07;
            memcpy(data, &(L1Cache[index]), WORD_SIZE);
        }
        time += L1_READ_TIME;
    }

    if (mode == MODE_WRITE) {
        // write data from cache line
        if (!(address % 8)) {
            // even word on block
            memcpy(&(L1Cache[index]), data, WORD_SIZE);
        } else {
            // odd word on block
            memcpy(&(L1Cache[index]), data, WORD_SIZE);
        }
        time += L1_WRITE_TIME;
        Line->Dirty = 1;
    }
}

void read(uint32_t address, uint8_t *data) {
    accessL1(address, data, MODE_READ);
}

void write(uint32_t address, uint8_t *data) {
    accessL1(address, data, MODE_WRITE);
}
