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

#include "xxhash.h"
#include <string.h>

/* Rotleft macro */
#define XXH_rotl32(x, r) (((x) << (r)) | ((x) >> (32 - (r))))

uint32_t xxhash32(const void* input, size_t len, uint32_t seed) {
    const uint8_t* p = (const uint8_t*)input;
    const uint8_t* end = p + len;
    uint32_t h32;
    
    if (len >= 16) {
        const uint8_t* limit = end - 16;
        uint32_t v1 = seed + XXH_PRIME32_1 + XXH_PRIME32_2;
        uint32_t v2 = seed + XXH_PRIME32_2;
        uint32_t v3 = seed + 0;
        uint32_t v4 = seed - XXH_PRIME32_1;
        
        do {
            v1 = XXH_rotl32(v1 + (*(uint32_t*)p) * XXH_PRIME32_2, 13) * XXH_PRIME32_1;
            p += 4;
            v2 = XXH_rotl32(v2 + (*(uint32_t*)p) * XXH_PRIME32_2, 13) * XXH_PRIME32_1;
            p += 4;
            v3 = XXH_rotl32(v3 + (*(uint32_t*)p) * XXH_PRIME32_2, 13) * XXH_PRIME32_1;
            p += 4;
            v4 = XXH_rotl32(v4 + (*(uint32_t*)p) * XXH_PRIME32_2, 13) * XXH_PRIME32_1;
            p += 4;
        } while (p <= limit);
        
        h32 = XXH_rotl32(v1, 1) + XXH_rotl32(v2, 7) + 
              XXH_rotl32(v3, 12) + XXH_rotl32(v4, 18);
    } else {
        h32 = seed + XXH_PRIME32_5;
    }
    
    h32 += (uint32_t)len;
    
    while (p + 4 <= end) {
        h32 += (*(uint32_t*)p) * XXH_PRIME32_3;
        h32 = XXH_rotl32(h32, 17) * XXH_PRIME32_4;
        p += 4;
    }
    
    while (p < end) {
        h32 += (*p++) * XXH_PRIME32_5;
        h32 = XXH_rotl32(h32, 11) * XXH_PRIME32_1;
    }
    
    h32 ^= h32 >> 15;
    h32 *= XXH_PRIME32_2;
    h32 ^= h32 >> 13;
    h32 *= XXH_PRIME32_3;
    h32 ^= h32 >> 16;
    
    return h32;
}