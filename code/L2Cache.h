#ifndef L2CACHE_H
#define L2CACHE_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "Cache.h"

void accessDRAM(uint32_t address, uint8_t *data, uint32_t mode);

void initCacheL2();

void accessL2(uint32_t, uint8_t *, uint32_t);

typedef struct CacheLine {
    uint8_t Valid;
    uint8_t Dirty;
    uint32_t Tag;
} CacheLine;

typedef struct Cache2 {
    uint32_t init;
    CacheLine line[L2_SIZE / BLOCK_SIZE];
} Cache2;

#endif
