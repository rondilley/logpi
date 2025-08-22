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

#include "mempool.h"
#include "mem.h"
#include <stdlib.h>
#include <string.h>

/* Align size to boundary */
static inline size_t align_size(size_t size, size_t alignment) {
    return (size + alignment - 1) & ~(alignment - 1);
}

/* Allocate new memory block */
static pool_block_t *allocate_block(size_t min_size) {
    size_t block_size = POOL_BLOCK_SIZE;
    pool_block_t *block;
    
    /* Ensure block is large enough */
    if (min_size > POOL_BLOCK_SIZE - sizeof(pool_block_t)) {
        block_size = min_size + sizeof(pool_block_t) + POOL_ALIGNMENT;
    }
    
    block = (pool_block_t *)XMALLOC(block_size);
    if (UNLIKELY(!block)) {
        return NULL;
    }
    
    block->next = NULL;
    block->current = (char *)block->data;
    block->end = (char *)block + block_size;
    
    return block;
}

/* Create memory pool */
mempool_t *mempool_create(void) {
    mempool_t *pool = (mempool_t *)XMALLOC(sizeof(mempool_t));
    if (UNLIKELY(!pool)) {
        return NULL;
    }
    
    memset(pool, 0, sizeof(mempool_t));
    
    /* Allocate initial block */
    pool->current_block = allocate_block(0);
    if (UNLIKELY(!pool->current_block)) {
        XFREE(pool);
        return NULL;
    }
    
    pool->block_count = 1;
    return pool;
}

/* Allocate memory from pool */
void *mempool_alloc(mempool_t *pool, size_t size) {
    pool_block_t *block;
    void *ptr;
    size_t aligned_size;
    
    if (UNLIKELY(!pool || size == 0)) {
        return NULL;
    }
    
    aligned_size = align_size(size, POOL_ALIGNMENT);
    block = pool->current_block;
    
    /* Try current block */
    if (LIKELY(block && (block->current + aligned_size <= block->end))) {
        ptr = block->current;
        block->current += aligned_size;
        pool->total_allocated += aligned_size;
        return ptr;
    }
    
    /* Need new block */
    if (pool->free_blocks) {
        /* Reuse free block */
        block = pool->free_blocks;
        pool->free_blocks = block->next;
        block->current = (char *)block->data;
        
        /* Check if reused block is large enough */
        if (block->current + aligned_size <= block->end) {
            block->next = pool->current_block;
            pool->current_block = block;
            
            ptr = block->current;
            block->current += aligned_size;
            pool->total_allocated += aligned_size;
            return ptr;
        }
    }
    
    /* Allocate new block */
    block = allocate_block(size);
    if (UNLIKELY(!block)) {
        return NULL;
    }
    
    /* Link new block */
    block->next = pool->current_block;
    pool->current_block = block;
    pool->block_count++;
    
    /* Allocate from new block */
    ptr = block->current;
    block->current += aligned_size;
    pool->total_allocated += aligned_size;
    
    return ptr;
}

/* Allocate aligned memory from pool */
void *mempool_alloc_aligned(mempool_t *pool, size_t size, size_t alignment) {
    void *ptr;
    uintptr_t addr;
    size_t offset;
    
    if (UNLIKELY(!pool || size == 0 || alignment == 0)) {
        return NULL;
    }
    
    /* For common alignments, use fast path */
    if (alignment <= POOL_ALIGNMENT) {
        return mempool_alloc(pool, size);
    }
    
    /* Allocate extra space for alignment */
    ptr = mempool_alloc(pool, size + alignment - 1);
    if (UNLIKELY(!ptr)) {
        return NULL;
    }
    
    /* Align pointer */
    addr = (uintptr_t)ptr;
    offset = alignment - (addr % alignment);
    if (offset == alignment) offset = 0;
    
    return (void *)(addr + offset);
}

/* Reset pool (keep blocks for reuse) */
void mempool_reset(mempool_t *pool) {
    pool_block_t *block;
    
    if (UNLIKELY(!pool)) {
        return;
    }
    
    /* Reset all blocks */
    for (block = pool->current_block; block; block = block->next) {
        block->current = (char *)block->data;
    }
    
    /* Move all blocks to free list except first */
    if (pool->current_block && pool->current_block->next) {
        pool_block_t *last_free = pool->current_block->next;
        
        /* Find end of chain */
        while (last_free->next) {
            last_free = last_free->next;
        }
        
        /* Link to existing free blocks */
        last_free->next = pool->free_blocks;
        pool->free_blocks = pool->current_block->next;
        pool->current_block->next = NULL;
    }
    
    pool->total_freed += pool->total_allocated;
    pool->total_allocated = 0;
}

/* Destroy pool */
void mempool_destroy(mempool_t *pool) {
    pool_block_t *block, *next;
    
    if (UNLIKELY(!pool)) {
        return;
    }
    
    /* Free all blocks */
    for (block = pool->current_block; block; block = next) {
        next = block->next;
        XFREE(block);
    }
    
    for (block = pool->free_blocks; block; block = next) {
        next = block->next;
        XFREE(block);
    }
    
    XFREE(pool);
}

/* Get memory usage statistics */
size_t mempool_get_usage(mempool_t *pool) {
    if (UNLIKELY(!pool)) {
        return 0;
    }
    
    return pool->total_allocated + (pool->block_count * sizeof(pool_block_t));
}