/*****
 *
 * Description: High-Performance Memory Pool Implementation
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

#ifndef MEMPOOL_H
#define MEMPOOL_H

#include <stddef.h>
#include <stdint.h>
#include "../include/common.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Memory pool configuration */
#define POOL_BLOCK_SIZE 8192    /* 8KB blocks */
#define POOL_ALIGNMENT 16       /* 16-byte alignment for SIMD */

/* Memory pool block */
typedef struct pool_block_s {
    struct pool_block_s *next;
    char *current;              /* Current allocation pointer */
    char *end;                  /* End of usable memory */
    char data[];                /* Flexible array for memory */
} pool_block_t;

/* Memory pool */
typedef struct mempool_s {
    pool_block_t *current_block;
    pool_block_t *free_blocks;  /* Free blocks list */
    size_t total_allocated;
    size_t total_freed;
    size_t block_count;
} mempool_t;

/* Function prototypes */
mempool_t *mempool_create(void);
void *mempool_alloc(mempool_t *pool, size_t size);
void *mempool_alloc_aligned(mempool_t *pool, size_t size, size_t alignment);
void mempool_reset(mempool_t *pool);
void mempool_destroy(mempool_t *pool);
size_t mempool_get_usage(mempool_t *pool);

/* Inline fast allocation for small objects */
static ALWAYS_INLINE void *mempool_alloc_fast(mempool_t *pool, size_t size) {
    pool_block_t *block = pool->current_block;
    
    if (LIKELY(block && (block->current + size <= block->end))) {
        void *ptr = block->current;
        block->current += (size + POOL_ALIGNMENT - 1) & ~(POOL_ALIGNMENT - 1);
        return ptr;
    }
    
    /* Slow path - need new block */
    return mempool_alloc(pool, size);
}

#ifdef __cplusplus
}
#endif

#endif /* MEMPOOL_H */