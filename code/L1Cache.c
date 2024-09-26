#include "L1Cache.h"

uint32_t time;
uint8_t DRAM[DRAM_SIZE];

uint8_t L1Cache[L1_SIZE];
Cache1 L1;


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


/********************************  Cache *******************************/

void initCacheL1() {
    for (int i = 0; i < (L1_SIZE / BLOCK_SIZE); i++) {
        L1.line[i].Valid = 0;
        L1.line[i].Dirty = 0;
        L1.line[i].Tag = 0;
    }
    L1.init = 1;
}

void initCache() {
    initCacheL1();
    initCacheL2();
}

void accessL1(uint32_t address, uint8_t *data, uint32_t mode) {
    uint32_t index, Tag, MemAddress;
    uint8_t TempBlock[BLOCK_SIZE];

    Tag = address >> 14; // Use the most significant 18 bits to determine the Tag (32 - 6 - 8)
    index = (address >> 6) & 0xFF; // Use the next 8 bits to determine the index (256 linhas, 0xFF = 255)

    MemAddress = address & ~(BLOCK_SIZE - 1); // Memory address of the block

    /* init cache */
    if (L1.init == 0) {
        initCacheL1();
    }

    CacheLine *Line = &L1.line[index];

    /* access Cache */
    if (!Line->Valid || Line->Tag != Tag) {
        // if block not present - miss
        accessL2(MemAddress, TempBlock, MODE_READ); // get new block from L2
        time += L2_READ_TIME; // Increment cache L2 read time
      
        if ((Line->Valid) && (Line->Dirty)) {
            MemAddress = (Line->Tag << 6) | (index << 6); // get address of the block in memory
            accessL2(MemAddress, &L1Cache[index * BLOCK_SIZE], MODE_WRITE); // then write back old block
            time += L2_WRITE_TIME; // Increment cache L2 write time
        }

        memcpy(&L1Cache[index * BLOCK_SIZE], TempBlock, BLOCK_SIZE); // copy new block to cache line
        Line->Valid = 1;
        Line->Tag = Tag;
        Line->Dirty = 0;
    } // if miss, then replaced with the correct block

    if (mode == MODE_READ) {
        // read data from cache line
        memcpy(data, &L1Cache[index * BLOCK_SIZE + (address & (BLOCK_SIZE - 1))], WORD_SIZE);
        time += L1_READ_TIME; // Increment cache L1 read time
    }
    if (mode == MODE_WRITE) {
        // write data to cache line
        memcpy(&L1Cache[index * BLOCK_SIZE + (address & (BLOCK_SIZE - 1))], data, WORD_SIZE);
        time += L1_WRITE_TIME; // Increment cache L1 write time
        Line->Dirty = 1;
    }
}

void read(uint32_t address, uint8_t *data) {
    accessL1(address, data, MODE_READ);
}

void write(uint32_t address, uint8_t *data) {
    accessL1(address, data, MODE_WRITE);
}