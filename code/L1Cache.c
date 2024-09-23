#include "L1Cache.h"

uint8_t L1Cache[L1_SIZE];
uint8_t L2Cache[L2_SIZE];
uint8_t DRAM[DRAM_SIZE];
uint32_t time;

Cache L1;
Cache L2;

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
void initCacheL1() {
    L1.init = 0;
    for (int i = 0; i < (L1_SIZE / BLOCK_SIZE); i++) {
        L1.line[i].Valid = 0;
        L1.line[i].Dirty = 0;
        L1.line[i].Tag = 0;
    }
    L1.init = 1;
}

void initCacheL2() {
    L2.init = 0;
    for (int i = 0; i < (L2_SIZE / BLOCK_SIZE); i++) {
        L2.line[i].Valid = 0;
        L2.line[i].Dirty = 0;
        L2.line[i].Tag = 0;
    }
    L2.init = 1;
}

void initCache() {
    initCacheL1();
    initCacheL2();
}

void accessL1(uint32_t address, uint8_t *data, uint32_t mode) {
    uint32_t index, Tag, MemAddress;
    uint8_t TempBlock[BLOCK_SIZE];

    Tag = address >> 14; // Usar os 18 bits mais significativos para o Tag (32 - 6 - 8)
    index = (address >> 6) & 0xFF; // Usar os 8 bits seguintes para o índice (256 linhas, 0xFF = 255)

    MemAddress = address & ~(BLOCK_SIZE - 1); // Endereço do bloco na memória

    /* init cache */
    if (L1.init == 0) {
        initCache();
    }

    CacheLine *Line = &L1.line[index];

    /* access Cache*/
    if (!Line->Valid || Line->Tag != Tag) {
        // if block not present - miss
        accessL2(MemAddress, TempBlock, MODE_READ); // get new block from L2
      
        if ((Line->Valid) && (Line->Dirty)) {
            MemAddress = (Line->Tag << 6) | (index << 6); // get address of the block in memory
            accessL2(MemAddress, &L1Cache[index * BLOCK_SIZE], MODE_WRITE); // then write back old block
        }

        memcpy(&L1Cache[index * BLOCK_SIZE], TempBlock, BLOCK_SIZE); // copy new block to cache line
        Line->Valid = 1;
        Line->Tag = Tag;
        Line->Dirty = 0;
    } // if miss, then replaced with the correct block

    if (mode == MODE_READ) {
        // read data from cache line
        memcpy(data, &L1Cache[index * BLOCK_SIZE + (address & (BLOCK_SIZE - 1))], WORD_SIZE);
        time += L1_READ_TIME; // Incrementar o tempo para leitura do cache
    }
    if (mode == MODE_WRITE) {
        // write data to cache line
        memcpy(&L1Cache[index * BLOCK_SIZE + (address & (BLOCK_SIZE - 1))], data, WORD_SIZE);
        time += L1_WRITE_TIME; // Incrementar o tempo para escrita no cache
        Line->Dirty = 1;
    }
}

/*********************** L2 cache *************************/
void accessL2(uint32_t address, uint8_t *data, uint32_t mode) {
    uint32_t index, Tag, MemAddress;
    uint8_t TempBlock[BLOCK_SIZE];

    Tag = address >> 15; // Usar os 17 bits mais significativos para o Tag (32 - 6 - 9)
    index = (address >> 6) & 0x1FF; // Usar os 9 bits seguintes para o índice (512 linhas, 0x1FF = 511)

    MemAddress = address & ~(BLOCK_SIZE - 1); // Endereço do bloco na memória

    /* init cache */
    if (L2.init == 0) {
        initCacheL2();
    }

    CacheLine *Line = &L2.line[index];

    /* access Cache*/
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
        time += L2_READ_TIME; // Incrementar o tempo para leitura do cache
    }
    if (mode == MODE_WRITE) {
        // write data to cache line
        memcpy(&L2Cache[index * BLOCK_SIZE + (address & (BLOCK_SIZE - 1))], data, WORD_SIZE);
        time += L2_WRITE_TIME; // Incrementar o tempo para escrita no cache
        Line->Dirty = 1;
    }
}

void read(uint32_t address, uint8_t *data) {
    accessL1(address, data, MODE_READ);
}

void write(uint32_t address, uint8_t *data) {
    accessL1(address, data, MODE_WRITE);
}