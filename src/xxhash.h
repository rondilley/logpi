/*****
 *
 * Description: xxHash Implementation - Fast non-cryptographic hash
 *
 * Copyright (c) 2025, Ron Dilley
 * All rights reserved.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 ****/

#ifndef XXHASH_H
#define XXHASH_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* xxHash constants */
#define XXH_PRIME32_1   0x9E3779B1U
#define XXH_PRIME32_2   0x85EBCA77U
#define XXH_PRIME32_3   0xC2B2AE3DU
#define XXH_PRIME32_4   0x27D4EB2FU
#define XXH_PRIME32_5   0x165667B1U

/* Fast 32-bit xxHash implementation */
uint32_t xxhash32(const void* input, size_t len, uint32_t seed);

/* Optimized inline version for small keys */
static inline uint32_t xxhash32_small(const void* input, size_t len, uint32_t seed) {
    const uint8_t* p = (const uint8_t*)input;
    const uint8_t* end = p + len;
    uint32_t h32;
    
    if (len >= 16) {
        return xxhash32(input, len, seed);
    }
    
    h32 = seed + XXH_PRIME32_5;
    h32 += (uint32_t)len;
    
    while (p + 4 <= end) {
        h32 += (*(uint32_t*)p) * XXH_PRIME32_3;
        h32 = ((h32 << 17) | (h32 >> 15)) * XXH_PRIME32_4;
        p += 4;
    }
    
    while (p < end) {
        h32 += (*p++) * XXH_PRIME32_5;
        h32 = ((h32 << 11) | (h32 >> 21)) * XXH_PRIME32_1;
    }
    
    h32 ^= h32 >> 15;
    h32 *= XXH_PRIME32_2;
    h32 ^= h32 >> 13;
    h32 *= XXH_PRIME32_3;
    h32 ^= h32 >> 16;
    
    return h32;
}

#ifdef __cplusplus
}
#endif

#endif /* XXHASH_H */