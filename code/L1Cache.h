#ifndef L1CACHE_H
#define L1CACHE_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "Cache.h"
#include "L2Cache.h"

void resetTime();

uint32_t getTime();

void accessDRAM(uint32_t address, uint8_t *data, uint32_t mode);


void initCache();

void initCacheL1();

void accessL1(uint32_t address, uint8_t *data, uint32_t mode);

typedef struct Cache1 {
    uint32_t init;
    CacheLine line[L1_SIZE / BLOCK_SIZE];
} Cache1;

void read(uint32_t address, uint8_t *data);

void write(uint32_t address, uint8_t *data);

#endif
