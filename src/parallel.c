/*****
 *
 * Description: Parallel Processing Functions
 *
 * Copyright (c) 2025, Ron Dilley
 * All rights reserved.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 ****/

/****
 *
 * includes
 *
 ****/

#include "parallel.h"
#include "parser.h"
#include "mem.h"
#include "util.h"
#include <sys/stat.h>
#include <unistd.h>
#include <time.h>

/****
 *
 * local variables
 *
 ****/

/****
 *
 * external variables
 *
 ****/

extern Config_t *config;
extern volatile int quit;
extern int reload;

/****
 *
 * functions
 *
 ****/

/****
 *
 * get number of available CPU cores
 *
 ****/

int get_available_cores(void) {
  int cores = 1;
  
#ifdef _SC_NPROCESSORS_ONLN
  cores = sysconf(_SC_NPROCESSORS_ONLN);
  if (cores < 1) cores = 1;
#elif defined(_SC_NPROCESSORS_CONF)
  cores = sysconf(_SC_NPROCESSORS_CONF);
  if (cores < 1) cores = 1;
#else
  /* Default to 1 if we can't detect */
  cores = 1;
#endif
  
  if (cores > MAX_THREADS) cores = MAX_THREADS;
  
#ifdef DEBUG
  if (config->debug >= 2)
    fprintf(stderr, "DEBUG - Detected %d CPU cores\n", cores);
#endif
  
  return cores;
}

/****
 *
 * determine if parallel processing should be used
 *
 ****/

int should_use_parallel(off_t file_size, int available_cores) {
  /* Don't use parallel for small files */
  if (file_size < MIN_FILE_SIZE_FOR_PARALLEL) {
    return FALSE;
  }
  
  /* Need at least 2 cores */
  if (available_cores < 2) {
    return FALSE;
  }
  
  /* Check if auto_lpi_naming is enabled (single file mode) */
  if (!config->auto_lpi_naming) {
    /* In stdout mode, parallel might complicate output ordering */
    return FALSE;
  }
  
  return TRUE;
}

/****
 *
 * get file size
 *
 ****/

off_t get_file_size(FILE *file) {
  struct stat st;
  
  if (fstat(fileno(file), &st) == 0) {
    return st.st_size;
  }
  
  /* Fallback: seek to end */
  off_t current = ftello(file);
  fseeko(file, 0, SEEK_END);
  off_t size = ftello(file);
  fseeko(file, current, SEEK_SET);
  
  return size;
}

/****
 *
 * find next line boundary from offset
 *
 ****/

int find_line_boundary(FILE *file, off_t offset) {
  char buffer[4096];
  size_t bytes_read;
  
  if (fseeko(file, offset, SEEK_SET) != 0) {
    return -1;
  }
  
  bytes_read = fread(buffer, 1, sizeof(buffer), file);
  if (bytes_read == 0) {
    return 0;  /* End of file */
  }
  
  /* Find first newline */
  for (size_t i = 0; i < bytes_read; i++) {
    if (buffer[i] == '\n') {
      return i + 1;  /* Position after newline */
    }
  }
  
  return bytes_read;  /* No newline found in buffer */
}

/****
 *
 * initialize parallel processing context
 *
 ****/

parallel_context_t *init_parallel_context(const char *filename, FILE *file, struct hash_s *hash) {
  parallel_context_t *ctx;
  
  ctx = (parallel_context_t *)XMALLOC(sizeof(parallel_context_t));
  if (ctx == NULL) {
    fprintf(stderr, "ERR - Unable to allocate parallel context\n");
    return NULL;
  }
  XMEMSET(ctx, 0, sizeof(parallel_context_t));
  
  ctx->filename = filename;
  ctx->file = file;
  ctx->global_hash = hash;
  ctx->file_size = get_file_size(file);
  
  /* Initialize hash rwlock for thread-safe hash growth */
  if (pthread_rwlock_init(&ctx->hash_rwlock, NULL) != 0) {
    fprintf(stderr, "ERR - Unable to initialize hash rwlock\n");
    XFREE(ctx);
    return NULL;
  }
  
  /* Determine number of worker threads */
  int cores = get_available_cores();
  int threads = cores / 2;  /* Use half the cores by default */
  if (threads < 2) threads = 2;
  if (threads > 8) threads = 8;  /* Cap at 8 threads per file */
  
  /* Calculate chunk size */
  ctx->chunk_size = ctx->file_size / threads;
  if (ctx->chunk_size < MIN_CHUNK_SIZE) {
    ctx->chunk_size = MIN_CHUNK_SIZE;
    threads = ctx->file_size / MIN_CHUNK_SIZE;
    if (threads < 2) threads = 2;
  }
  if (ctx->chunk_size > DEFAULT_CHUNK_SIZE) {
    ctx->chunk_size = DEFAULT_CHUNK_SIZE;
  }
  
#ifdef DEBUG
  if (config->debug >= 1) {
    fprintf(stderr, "DEBUG - Parallel processing: %ld MB file, %d threads, %ld MB chunks\n",
            ctx->file_size / 1048576, threads, ctx->chunk_size / 1048576);
  }
#endif
  
  /* Create thread pool */
  ctx->pool = create_thread_pool(threads);
  if (ctx->pool == NULL) {
    XFREE(ctx);
    return NULL;
  }
  
  /* Set back pointer for workers to access context */
  ctx->pool->ctx = ctx;
  
  /* Initialize chunk dispatcher */
  ctx->pool->dispatcher = init_chunk_dispatcher(file, ctx->file_size, ctx->chunk_size);
  if (ctx->pool->dispatcher == NULL) {
    destroy_thread_pool(ctx->pool);
    XFREE(ctx);
    return NULL;
  }
  
  return ctx;
}

/****
 *
 * free parallel context
 *
 ****/

void free_parallel_context(parallel_context_t *ctx) {
  if (ctx == NULL) return;
  
  if (ctx->pool) {
    /* Don't free dispatcher here - destroy_thread_pool handles it */
    destroy_thread_pool(ctx->pool);
  }
  
  /* Destroy hash rwlock */
  pthread_rwlock_destroy(&ctx->hash_rwlock);
  
  XFREE(ctx);
}

/****
 *
 * create thread pool
 *
 ****/

thread_pool_t *create_thread_pool(int num_threads) {
  thread_pool_t *pool;
  
  pool = (thread_pool_t *)XMALLOC(sizeof(thread_pool_t));
  if (pool == NULL) {
    fprintf(stderr, "ERR - Unable to allocate thread pool\n");
    return NULL;
  }
  XMEMSET(pool, 0, sizeof(thread_pool_t));
  
  pool->num_workers = num_threads;
  pool->workers = (worker_data_t *)XMALLOC(sizeof(worker_data_t) * num_threads);
  if (pool->workers == NULL) {
    fprintf(stderr, "ERR - Unable to allocate worker threads\n");
    XFREE(pool);
    return NULL;
  }
  XMEMSET(pool->workers, 0, sizeof(worker_data_t) * num_threads);
  
  /* Initialize mutex and condition variables */
  pthread_mutex_init(&pool->pool_mutex, NULL);
  pthread_cond_init(&pool->work_done, NULL);
  
  /* Create chunk queue for producer-consumer */
  pool->chunk_queue = create_chunk_queue(16);  /* Queue capacity of 16 chunks */
  if (pool->chunk_queue == NULL) {
    fprintf(stderr, "ERR - Unable to create chunk queue\n");
    XFREE(pool->workers);
    pthread_mutex_destroy(&pool->pool_mutex);
    pthread_cond_destroy(&pool->work_done);
    XFREE(pool);
    return NULL;
  }
  
  /* Create address queue for parser->hash communication */
  pool->address_queue = create_address_queue(50000);  /* Queue capacity of 50K hash operations */
  if (pool->address_queue == NULL) {
    fprintf(stderr, "ERR - Unable to create address queue\n");
    destroy_chunk_queue(pool->chunk_queue);
    XFREE(pool->workers);
    pthread_mutex_destroy(&pool->pool_mutex);
    pthread_cond_destroy(&pool->work_done);
    XFREE(pool);
    return NULL;
  }
  
  /* Initialize workers */
  for (int i = 0; i < num_threads; i++) {
    pool->workers[i].thread_id = i;
    pool->workers[i].status = 0;  /* idle */
    pool->workers[i].pool = pool;  /* Set back pointer */
    /* Add 64KB overhead for carry-forward buffer */
    pool->workers[i].max_chunk_size = DEFAULT_CHUNK_SIZE + 65536;
    
    /* Pre-allocate chunk structure */
    pool->workers[i].chunk = (chunk_t *)XMALLOC(sizeof(chunk_t));
    if (pool->workers[i].chunk == NULL) {
      fprintf(stderr, "ERR - Unable to allocate chunk structure for thread %d\n", i);
      /* Clean up and return */
      for (int j = 0; j < i; j++) {
        if (pool->workers[j].chunk_buffer) XFREE(pool->workers[j].chunk_buffer);
        if (pool->workers[j].chunk) XFREE(pool->workers[j].chunk);
      }
      destroy_address_queue(pool->address_queue);
      destroy_chunk_queue(pool->chunk_queue);
      XFREE(pool->workers);
      pthread_mutex_destroy(&pool->pool_mutex);
      pthread_cond_destroy(&pool->work_done);
      XFREE(pool);
      return NULL;
    }
    XMEMSET(pool->workers[i].chunk, 0, sizeof(chunk_t));
    
    /* Pre-allocate chunk buffer */
    pool->workers[i].chunk_buffer = (char *)XMALLOC(pool->workers[i].max_chunk_size + 1);
    if (pool->workers[i].chunk_buffer == NULL) {
      fprintf(stderr, "ERR - Unable to allocate chunk buffer for thread %d\n", i);
      XFREE(pool->workers[i].chunk);
      /* Clean up and return */
      for (int j = 0; j < i; j++) {
        if (pool->workers[j].chunk_buffer) XFREE(pool->workers[j].chunk_buffer);
        if (pool->workers[j].chunk) XFREE(pool->workers[j].chunk);
      }
      destroy_address_queue(pool->address_queue);
      destroy_chunk_queue(pool->chunk_queue);
      XFREE(pool->workers);
      pthread_mutex_destroy(&pool->pool_mutex);
      pthread_cond_destroy(&pool->work_done);
      XFREE(pool);
      return NULL;
    }
    
    /* Allocate local address buffer for batching (1024 addresses per batch) */
    pool->workers[i].local_buffer_capacity = 1024;
    pool->workers[i].local_buffer = (hash_operation_entry_t *)XMALLOC(sizeof(hash_operation_entry_t) * pool->workers[i].local_buffer_capacity);
    if (pool->workers[i].local_buffer == NULL) {
      fprintf(stderr, "ERR - Unable to allocate local buffer for thread %d\n", i);
      XFREE(pool->workers[i].chunk_buffer);
      XFREE(pool->workers[i].chunk);
      /* Clean up and return */
      for (int j = 0; j < i; j++) {
        if (pool->workers[j].local_buffer) XFREE(pool->workers[j].local_buffer);
        if (pool->workers[j].chunk_buffer) XFREE(pool->workers[j].chunk_buffer);
        if (pool->workers[j].chunk) XFREE(pool->workers[j].chunk);
      }
      destroy_address_queue(pool->address_queue);
      destroy_chunk_queue(pool->chunk_queue);
      XFREE(pool->workers);
      pthread_mutex_destroy(&pool->pool_mutex);
      pthread_cond_destroy(&pool->work_done);
      XFREE(pool);
      return NULL;
    }
    pool->workers[i].local_buffer_count = 0;
    
    /* No local hash needed - worker will send addresses to hash thread */
  }
  
  return pool;
}

/****
 *
 * destroy thread pool
 *
 ****/

void destroy_thread_pool(thread_pool_t *pool) {
  if (pool == NULL) return;
  
  /* Signal shutdown */
  pthread_mutex_lock(&pool->pool_mutex);
  pool->shutdown = 1;
  pthread_mutex_unlock(&pool->pool_mutex);
  
  /* Signal chunk queue shutdown */
  if (pool->chunk_queue) {
    pthread_mutex_lock(&pool->chunk_queue->queue_mutex);
    pool->chunk_queue->finished = 1;
    pthread_cond_broadcast(&pool->chunk_queue->not_empty);
    pthread_mutex_unlock(&pool->chunk_queue->queue_mutex);
  }
  
  /* Signal address queue shutdown */
  if (pool->address_queue) {
    pthread_mutex_lock(&pool->address_queue->queue_mutex);
    pool->address_queue->finished = 1;
    pool->address_queue->active_producers = 0;
    pthread_cond_broadcast(&pool->address_queue->not_empty);
    pthread_mutex_unlock(&pool->address_queue->queue_mutex);
  }
  
  /* Stop I/O thread if running */
  if (pool->io_thread_created) {
    pthread_join(pool->io_thread, NULL);
  }
  
  /* Stop hash thread if running */
  if (pool->hash_thread_created) {
    pthread_join(pool->hash_thread, NULL);
  }
  
  /* Wait for worker threads to finish */
  for (int i = 0; i < pool->num_workers; i++) {
    if (pool->workers[i].thread) {
      pthread_join(pool->workers[i].thread, NULL);
    }
    /* No local hash to free - using shared hash thread */
    if (pool->workers[i].local_buffer) {
      XFREE(pool->workers[i].local_buffer);
    }
    if (pool->workers[i].chunk_buffer) {
      XFREE(pool->workers[i].chunk_buffer);
    }
    if (pool->workers[i].chunk) {
      XFREE(pool->workers[i].chunk);
    }
  }
  
  /* Clean up chunk queue */
  if (pool->chunk_queue) {
    destroy_chunk_queue(pool->chunk_queue);
    pool->chunk_queue = NULL;
  }
  
  /* Clean up address queue */
  if (pool->address_queue) {
    destroy_address_queue(pool->address_queue);
    pool->address_queue = NULL;
  }
  
  /* Free dispatcher if exists */
  if (pool->dispatcher) {
    free_chunk_dispatcher(pool->dispatcher);
    pool->dispatcher = NULL;
  }
  
  pthread_mutex_destroy(&pool->pool_mutex);
  /* work_ready no longer needed with chunk queue */
  pthread_cond_destroy(&pool->work_done);
  
  XFREE(pool->workers);
  XFREE(pool);
}

/****
 *
 * initialize chunk dispatcher for streaming processing
 *
 ****/

chunk_dispatcher_t *init_chunk_dispatcher(FILE *file, off_t file_size, size_t chunk_size) {
  chunk_dispatcher_t *dispatcher;
  
  dispatcher = (chunk_dispatcher_t *)XMALLOC(sizeof(chunk_dispatcher_t));
  if (dispatcher == NULL) {
    fprintf(stderr, "ERR - Unable to allocate chunk dispatcher\n");
    return NULL;
  }
  XMEMSET(dispatcher, 0, sizeof(chunk_dispatcher_t));
  
  dispatcher->file = file;
  dispatcher->file_size = file_size;
  dispatcher->current_offset = 0;
  dispatcher->current_line_number = 1;
  dispatcher->chunks_dispatched = 0;
  dispatcher->chunks_completed = 0;
  dispatcher->target_chunk_size = chunk_size;
  
  /* Initialize carry forward buffer for partial lines */
  dispatcher->carry_forward_capacity = 65536;  /* 64KB buffer for partial lines */
  dispatcher->carry_forward_buffer = (char *)XMALLOC(dispatcher->carry_forward_capacity);
  if (dispatcher->carry_forward_buffer == NULL) {
    fprintf(stderr, "ERR - Unable to allocate carry forward buffer\n");
    XFREE(dispatcher);
    return NULL;
  }
  dispatcher->carry_forward_size = 0;
  
  /* Initialize timing */
  dispatcher->start_time = time(NULL);
  dispatcher->last_report_time = dispatcher->start_time;
  
  /* Initialize mutex for thread-safe file access */
  pthread_mutex_init(&dispatcher->file_mutex, NULL);
  
#ifdef DEBUG
  if (config->debug >= 1) {
    fprintf(stderr, "DEBUG - Initialized chunk dispatcher: %ld MB file, %zu MB target chunks\n",
            file_size / 1048576, chunk_size / 1048576);
  }
#endif
  
  return dispatcher;
}

/****
 *
 * free chunk dispatcher
 *
 ****/

void free_chunk_dispatcher(chunk_dispatcher_t *dispatcher) {
  if (dispatcher == NULL) return;
  
  if (dispatcher->carry_forward_buffer) {
    XFREE(dispatcher->carry_forward_buffer);
  }
  pthread_mutex_destroy(&dispatcher->file_mutex);
  XFREE(dispatcher);
}

/****
 *
 * create chunk queue for producer-consumer
 *
 ****/

chunk_queue_t *create_chunk_queue(int capacity) {
  chunk_queue_t *queue;
  
  queue = (chunk_queue_t *)XMALLOC(sizeof(chunk_queue_t));
  if (queue == NULL) {
    fprintf(stderr, "ERR - Unable to allocate chunk queue\n");
    return NULL;
  }
  XMEMSET(queue, 0, sizeof(chunk_queue_t));
  
  queue->chunks = (chunk_t **)XMALLOC(sizeof(chunk_t *) * capacity);
  if (queue->chunks == NULL) {
    fprintf(stderr, "ERR - Unable to allocate chunk queue array\n");
    XFREE(queue);
    return NULL;
  }
  
  queue->capacity = capacity;
  queue->count = 0;
  queue->head = 0;
  queue->tail = 0;
  queue->finished = 0;
  
  pthread_mutex_init(&queue->queue_mutex, NULL);
  pthread_cond_init(&queue->not_empty, NULL);
  pthread_cond_init(&queue->not_full, NULL);
  
  return queue;
}

/****
 *
 * destroy chunk queue
 *
 ****/

void destroy_chunk_queue(chunk_queue_t *queue) {
  if (queue == NULL) return;
  
  pthread_mutex_lock(&queue->queue_mutex);
  
  /* Free any remaining chunks */
  while (queue->count > 0) {
    chunk_t *chunk = queue->chunks[queue->head];
    if (chunk) {
      if (chunk->buffer) XFREE(chunk->buffer);
      XFREE(chunk);
    }
    queue->head = (queue->head + 1) % queue->capacity;
    queue->count--;
  }
  
  pthread_mutex_unlock(&queue->queue_mutex);
  
  pthread_mutex_destroy(&queue->queue_mutex);
  pthread_cond_destroy(&queue->not_empty);
  pthread_cond_destroy(&queue->not_full);
  
  XFREE(queue->chunks);
  XFREE(queue);
}

/****
 *
 * enqueue chunk (producer)
 *
 ****/

int enqueue_chunk(chunk_queue_t *queue, chunk_t *chunk) {
  if (queue == NULL) return FALSE;
  
  pthread_mutex_lock(&queue->queue_mutex);
  
  /* Wait for space in queue */
  while (queue->count >= queue->capacity && !queue->finished) {
    pthread_cond_wait(&queue->not_full, &queue->queue_mutex);
  }
  
  if (queue->finished) {
    pthread_mutex_unlock(&queue->queue_mutex);
    return FALSE;
  }
  
  /* Add chunk to queue */
  queue->chunks[queue->tail] = chunk;
  queue->tail = (queue->tail + 1) % queue->capacity;
  queue->count++;
  
  /* Signal consumers */
  pthread_cond_signal(&queue->not_empty);
  pthread_mutex_unlock(&queue->queue_mutex);
  
  return TRUE;
}

/****
 *
 * dequeue chunk (consumer)
 *
 ****/

chunk_t *dequeue_chunk(chunk_queue_t *queue) {
  chunk_t *chunk = NULL;
  
  if (queue == NULL) return NULL;
  
  pthread_mutex_lock(&queue->queue_mutex);
  
  /* Wait for chunk or finish signal */
  while (queue->count == 0 && !queue->finished) {
    pthread_cond_wait(&queue->not_empty, &queue->queue_mutex);
  }
  
  if (queue->count > 0) {
    /* Get chunk from queue */
    chunk = queue->chunks[queue->head];
    queue->head = (queue->head + 1) % queue->capacity;
    queue->count--;
    
    /* Signal producer */
    pthread_cond_signal(&queue->not_full);
  }
  
  pthread_mutex_unlock(&queue->queue_mutex);
  
  return chunk;
}

/****
 *
 * create address queue
 *
 ****/

address_queue_t *create_address_queue(int capacity) {
  address_queue_t *queue;
  
  queue = (address_queue_t *)XMALLOC(sizeof(address_queue_t));
  if (queue == NULL) {
    fprintf(stderr, "ERR - Unable to allocate address queue\n");
    return NULL;
  }
  XMEMSET(queue, 0, sizeof(address_queue_t));
  
  queue->entries = (hash_operation_entry_t *)XMALLOC(sizeof(hash_operation_entry_t) * capacity);
  if (queue->entries == NULL) {
    fprintf(stderr, "ERR - Unable to allocate address queue array\n");
    XFREE(queue);
    return NULL;
  }
  
  queue->capacity = capacity;
  queue->count = 0;
  queue->head = 0;
  queue->tail = 0;
  queue->finished = 0;
  queue->active_producers = 0;
  
  pthread_mutex_init(&queue->queue_mutex, NULL);
  pthread_cond_init(&queue->not_empty, NULL);
  pthread_cond_init(&queue->not_full, NULL);
  
  return queue;
}

/****
 *
 * destroy address queue
 *
 ****/

void destroy_address_queue(address_queue_t *queue) {
  if (queue == NULL) return;
  
  pthread_mutex_destroy(&queue->queue_mutex);
  pthread_cond_destroy(&queue->not_empty);
  pthread_cond_destroy(&queue->not_full);
  
  XFREE(queue->entries);
  XFREE(queue);
}

/****
 *
 * enqueue address (producer)
 *
 ****/

int enqueue_address(address_queue_t *queue, const char *address, unsigned int line_number, uint16_t field_offset, int worker_id) {
  if (queue == NULL || address == NULL) return FALSE;
  
  pthread_mutex_lock(&queue->queue_mutex);
  
  /* Wait for space in queue */
  while (queue->count >= queue->capacity && !queue->finished) {
    pthread_cond_wait(&queue->not_full, &queue->queue_mutex);
  }
  
  if (queue->finished) {
    pthread_mutex_unlock(&queue->queue_mutex);
    return FALSE;
  }
  
  /* Add entry to queue */
  address_entry_t *entry = &queue->entries[queue->tail];
  strncpy(entry->address, address, sizeof(entry->address) - 1);
  entry->address[sizeof(entry->address) - 1] = '\0';
  entry->line_number = line_number;
  entry->field_offset = field_offset;
  entry->worker_id = worker_id;
  
  queue->tail = (queue->tail + 1) % queue->capacity;
  queue->count++;
  
  /* Signal consumer */
  pthread_cond_signal(&queue->not_empty);
  
  pthread_mutex_unlock(&queue->queue_mutex);
  
  return TRUE;
}

/****
 *
 * enqueue hash operation (new distributed architecture)
 *
 ****/

int enqueue_hash_operation(address_queue_t *queue, hash_operation_t op_type, const char *address, unsigned int line_number, uint16_t field_offset, struct hashRec_s *hash_record, int worker_id) {
  if (queue == NULL || address == NULL) return FALSE;
  
  pthread_mutex_lock(&queue->queue_mutex);
  
  /* Wait for space in queue */
  while (queue->count >= queue->capacity && !queue->finished) {
    pthread_cond_wait(&queue->not_full, &queue->queue_mutex);
  }
  
  if (queue->finished) {
    pthread_mutex_unlock(&queue->queue_mutex);
    return FALSE;
  }
  
  /* Add entry to queue */
  hash_operation_entry_t *entry = &queue->entries[queue->tail];
  entry->op_type = op_type;
  strncpy(entry->address, address, sizeof(entry->address) - 1);
  entry->address[sizeof(entry->address) - 1] = '\0';
  entry->line_number = line_number;
  entry->field_offset = field_offset;
  entry->hash_record = hash_record;
  entry->worker_id = worker_id;
  
  queue->tail = (queue->tail + 1) % queue->capacity;
  queue->count++;
  
  /* Signal consumer */
  pthread_cond_signal(&queue->not_empty);
  
  pthread_mutex_unlock(&queue->queue_mutex);
  
  return TRUE;
}

/****
 *
 * dequeue address (consumer)
 *
 ****/

address_entry_t *dequeue_address(address_queue_t *queue) {
  static address_entry_t entry;
  
  if (queue == NULL) return NULL;
  
  pthread_mutex_lock(&queue->queue_mutex);
  
  /* Wait for entry or finish signal */
  while (queue->count == 0 && queue->active_producers > 0) {
    pthread_cond_wait(&queue->not_empty, &queue->queue_mutex);
  }
  
  if (queue->count > 0) {
    /* Copy entry from queue */
    entry = queue->entries[queue->head];
    queue->head = (queue->head + 1) % queue->capacity;
    queue->count--;
    
    /* Signal producer */
    pthread_cond_signal(&queue->not_full);
    
    pthread_mutex_unlock(&queue->queue_mutex);
    return &entry;
  }
  
  pthread_mutex_unlock(&queue->queue_mutex);
  return NULL;
}

/****
 *
 * dequeue hash operation (new distributed architecture)
 *
 ****/

hash_operation_entry_t *dequeue_hash_operation(address_queue_t *queue) {
  static hash_operation_entry_t entry;
  
  if (queue == NULL) return NULL;
  
  pthread_mutex_lock(&queue->queue_mutex);
  
  /* Wait for entries or shutdown */
  while (queue->count == 0 && queue->active_producers > 0 && !queue->finished) {
    pthread_cond_wait(&queue->not_empty, &queue->queue_mutex);
  }
  
  if (queue->count > 0) {
    entry = queue->entries[queue->head];
    queue->head = (queue->head + 1) % queue->capacity;
    queue->count--;
    
    /* Signal producer */
    pthread_cond_signal(&queue->not_full);
    
    pthread_mutex_unlock(&queue->queue_mutex);
    return &entry;
  }
  
  pthread_mutex_unlock(&queue->queue_mutex);
  return NULL;
}

/****
 *
 * check if worker has pending "add new IP" for this address in local buffer
 *
 ****/

int has_pending_new_address_in_buffer(worker_data_t *worker, const char *address) {
  for (int i = 0; i < worker->local_buffer_count; i++) {
    if (worker->local_buffer[i].op_type == HASH_OP_NEW_ADDRESS && 
        strcmp(worker->local_buffer[i].address, address) == 0) {
      return TRUE;
    }
  }
  return FALSE;
}

/****
 *
 * flush local address buffer to hash thread
 *
 ****/

int flush_local_buffer(worker_data_t *worker) {
  if (worker->local_buffer_count == 0) return TRUE;
  
  address_queue_t *queue = worker->pool->address_queue;
  pthread_mutex_lock(&queue->queue_mutex);
  
  /* Wait for space in queue for entire batch */
  while (queue->count + worker->local_buffer_count >= queue->capacity && !queue->finished) {
    pthread_cond_wait(&queue->not_full, &queue->queue_mutex);
  }
  
  if (queue->finished) {
    pthread_mutex_unlock(&queue->queue_mutex);
    return FALSE;
  }
  
  /* Copy entire batch to queue */
  for (int i = 0; i < worker->local_buffer_count; i++) {
    queue->entries[queue->tail] = worker->local_buffer[i];
    queue->tail = (queue->tail + 1) % queue->capacity;
    queue->count++;
  }
  
  /* Signal hash thread */
  pthread_cond_signal(&queue->not_empty);
  pthread_mutex_unlock(&queue->queue_mutex);
  
  worker->local_buffer_count = 0;
  return TRUE;
}

/****
 *
 * flush local buffer immediately for small batches 
 *
 ****/

int flush_local_buffer_immediate(worker_data_t *worker) {
  if (worker->local_buffer_count == 0) return TRUE;
  
  address_queue_t *queue = worker->pool->address_queue;
  pthread_mutex_lock(&queue->queue_mutex);
  
  /* Wait for space in queue for immediate small batch */
  while (queue->count + worker->local_buffer_count >= queue->capacity && !queue->finished) {
    pthread_cond_wait(&queue->not_full, &queue->queue_mutex);
  }
  
  if (queue->finished) {
    pthread_mutex_unlock(&queue->queue_mutex);
    return FALSE;
  }
  
  /* Copy immediate batch to queue */
  for (int i = 0; i < worker->local_buffer_count; i++) {
    queue->entries[queue->tail] = worker->local_buffer[i];
    queue->tail = (queue->tail + 1) % queue->capacity;
    queue->count++;
  }
  
  /* Signal hash thread immediately */
  pthread_cond_signal(&queue->not_empty);
  pthread_mutex_unlock(&queue->queue_mutex);
  
  worker->local_buffer_count = 0;
  return TRUE;
}

/****
 *
 * add address to local buffer (with batching)
 *
 ****/

/* 
 * NEW DISTRIBUTED ARCHITECTURE: Workers do hash lookups locally, 
 * only send operations to hash thread for writes
 */
int buffer_address_local(worker_data_t *worker, const char *address, unsigned int line_number, uint16_t field_offset) {
  struct hash_s *hash;
  struct hashRec_s *tmpRec;
  hash_operation_entry_t *entry;
  
  /* STEP 1: Check if we have pending "add new IP" for this address in local buffer */
  if (has_pending_new_address_in_buffer(worker, address)) {
    /* Flush immediately to resolve pending adds before doing hash lookup */
    if (!flush_local_buffer_immediate(worker)) {
      fprintf(stderr, "ERR - Failed to flush pending new address requests\n");
      return FALSE;
    }
  }
  
  /* STEP 2: Acquire read lock and do hash lookup */
  pthread_rwlock_rdlock(&worker->pool->ctx->hash_rwlock);
  hash = worker->pool->ctx->global_hash;
  tmpRec = getHashRecord(hash, address);
  pthread_rwlock_unlock(&worker->pool->ctx->hash_rwlock);
  
  if (tmpRec == NULL) {
    /* NEW ADDRESS: Send to hash thread for insertion */
    entry = &worker->local_buffer[worker->local_buffer_count];
    entry->op_type = HASH_OP_NEW_ADDRESS;
    strncpy(entry->address, address, sizeof(entry->address) - 1);
    entry->address[sizeof(entry->address) - 1] = '\0';
    entry->line_number = line_number;
    entry->field_offset = field_offset;
    entry->hash_record = NULL;  /* New address, no existing record */
    entry->worker_id = worker->thread_id;
    
    worker->local_buffer_count++;
    
    /* Use small batch sizes for new IP requests (1-5) to reduce race conditions */
    if (worker->local_buffer_count >= 5) {
      return flush_local_buffer_immediate(worker);
    }
  } else {
    /* EXISTING ADDRESS: Handle with per-thread array - NO CONTENTION! */
    metaData_t *tmpMd;
    location_array_t *thread_array;
    
    if (tmpRec->data != NULL) {
      tmpMd = (metaData_t *)tmpRec->data;
      
      /* Get this thread's location array for this address */
      thread_array = get_thread_location_array(tmpMd, worker->thread_id);
      if (thread_array == NULL) {
        fprintf(stderr, "ERR - Unable to get thread location array\n");
        return FALSE;
      }
      
      /* Add location to THIS THREAD's array - no blocking! */
      if (!add_location_atomic(thread_array, line_number, field_offset)) {
        /* Array is full, grow it directly (no hash thread needed) */
        size_t current_capacity = thread_array->capacity;
        size_t new_capacity;
        
        if (current_capacity >= 1048576) {  /* 1M entries = 16MB */
          new_capacity = current_capacity + (current_capacity / 4);  /* Grow by 25% */
        } else {
          new_capacity = current_capacity * 2;  /* Normal doubling */
        }
        
        if (grow_location_array(thread_array, new_capacity)) {
          /* Try again after growing */
          if (!add_location_atomic(thread_array, line_number, field_offset)) {
            fprintf(stderr, "ERR - Failed to add location after growing thread array\n");
            return FALSE;
          }
        } else {
          fprintf(stderr, "ERR - Failed to grow thread location array\n");
          return FALSE;
        }
      }
      
      /* Update this thread's count */
      tmpMd->thread_data[worker->thread_id].count++;
      
      /* Update total count atomically */
      __atomic_fetch_add(&tmpMd->total_count, 1, __ATOMIC_RELAXED);
    }
  }
  
  /* For existing address updates, use larger batch sizes for better throughput */
  if (worker->local_buffer_count >= worker->local_buffer_capacity) {
    return flush_local_buffer(worker);
  }
  
  return TRUE;
}

/****
 *
 * dedicated I/O thread (producer)
 *
 ****/

void *io_thread(void *arg) {
  thread_pool_t *pool = (thread_pool_t *)arg;
  chunk_dispatcher_t *dispatcher = pool->dispatcher;
  off_t current_offset = 0;
  unsigned int current_line_number = 0;
  unsigned int chunk_id = 0;
  time_t current_time;
  
#ifdef DEBUG
  if (config->debug >= 2)
    fprintf(stderr, "DEBUG - I/O thread started\n");
#endif
  
  while (current_offset < dispatcher->file_size && !pool->shutdown && !quit) {
    chunk_t *chunk;
    char *buffer;
    size_t buffer_size, bytes_to_read, bytes_read;
    off_t start_offset, end_offset;
    char *last_newline;
    unsigned int lines_in_chunk;
    
    /* Allocate chunk */
    chunk = (chunk_t *)XMALLOC(sizeof(chunk_t));
    if (chunk == NULL) {
      fprintf(stderr, "ERR - I/O thread: Unable to allocate chunk\n");
      break;
    }
    XMEMSET(chunk, 0, sizeof(chunk_t));
    
    /* Allocate buffer */
    buffer_size = dispatcher->target_chunk_size;
    if (current_offset + buffer_size > dispatcher->file_size) {
      buffer_size = dispatcher->file_size - current_offset;
    }
    
    /* Allocate buffer with space for carry-forward data */
    buffer = (char *)XMALLOC(dispatcher->target_chunk_size + dispatcher->carry_forward_capacity + 1);
    if (buffer == NULL) {
      fprintf(stderr, "ERR - I/O thread: Unable to allocate buffer\n");
      XFREE(chunk);
      break;
    }
    
    /* Add carry forward data first */
    size_t buffer_pos = 0;
    unsigned int carry_forward_lines = 0;
    if (dispatcher->carry_forward_size > 0) {
      memcpy(buffer, dispatcher->carry_forward_buffer, dispatcher->carry_forward_size);
      buffer_pos = dispatcher->carry_forward_size;
      
      /* Count lines in carry-forward buffer */
      for (size_t i = 0; i < dispatcher->carry_forward_size; i++) {
        if (dispatcher->carry_forward_buffer[i] == '\n') {
          carry_forward_lines++;
        }
      }
      
      dispatcher->carry_forward_size = 0;
    }
    
    /* Read new data - adjust read size to account for carry-forward data */
    bytes_to_read = buffer_size;
    if (buffer_pos > 0 && bytes_to_read > dispatcher->target_chunk_size - buffer_pos) {
      bytes_to_read = dispatcher->target_chunk_size - buffer_pos;
    }
    bytes_read = fread(buffer + buffer_pos, 1, bytes_to_read, dispatcher->file);
    if (bytes_read > 0) {
      buffer_pos += bytes_read;
      current_offset += bytes_read;
    }
    
    buffer[buffer_pos] = '\0';
    
    /* Find last complete line */
    last_newline = strrchr(buffer, '\n');
    if (last_newline != NULL) {
      size_t complete_size = (last_newline - buffer) + 1;
      size_t remainder_size = buffer_pos - complete_size;
      
      /* Store remainder for next chunk */
      if (remainder_size > 0 && remainder_size <= dispatcher->carry_forward_capacity) {
        memcpy(dispatcher->carry_forward_buffer, buffer + complete_size, remainder_size);
        dispatcher->carry_forward_size = remainder_size;
        /* No offset adjustment needed - we'll handle it with buffer management */
      }
      
      buffer_pos = complete_size;
    } else if (current_offset < dispatcher->file_size) {
      /* No complete lines but not EOF - this shouldn't happen with proper sizing */
      /* Process what we have */
    }
    
    /* Count lines */
    lines_in_chunk = 0;
    for (char *ptr = buffer; ptr < buffer + buffer_pos; ptr++) {
      if (*ptr == '\n') {
        lines_in_chunk++;
      }
    }
    
    /* Separate new lines from carry-forward lines */
    unsigned int new_lines = lines_in_chunk - carry_forward_lines;
    
    /* Fill chunk */
    chunk->chunk_id = chunk_id++;
    chunk->start_offset = current_offset - bytes_read;
    chunk->end_offset = current_offset;
    chunk->buffer = buffer;
    chunk->buffer_size = buffer_pos;
    /* The first line in this chunk's buffer starts at current_line_number */
    chunk->start_line_number = current_line_number;
    chunk->carry_forward_lines = carry_forward_lines;
    
    /* Update line counter for next chunk - advance by ONLY the new lines (not carry-forward) */
    current_line_number += new_lines;
    
    /* Check for signal-triggered reporting (matches serial mode) */
    if (reload == TRUE) {
      fprintf(stderr, "Processed %u lines/min\n", current_line_number);
      current_line_number = 0;  /* Reset counter for next minute */
      reload = FALSE;
    }
    
    /* Add chunk to queue for workers */
    if (!enqueue_chunk(pool->chunk_queue, chunk)) {
      /* Queue is shutting down */
      if (chunk->buffer) XFREE(chunk->buffer);
      XFREE(chunk);
      break;
    }
    
#ifdef DEBUG
    if (config->debug >= 3) {
      fprintf(stderr, "DEBUG - I/O thread produced chunk %d: %ld-%ld (%zu bytes, %u lines)\n",
              chunk->chunk_id, chunk->start_offset, chunk->end_offset, 
              chunk->buffer_size, lines_in_chunk);
    }
#endif
  }
  
  /* Signal end of chunks */
  pthread_mutex_lock(&pool->chunk_queue->queue_mutex);
  pool->chunk_queue->finished = 1;
  pthread_cond_broadcast(&pool->chunk_queue->not_empty);
  pthread_mutex_unlock(&pool->chunk_queue->queue_mutex);
  
#ifdef DEBUG
  if (config->debug >= 2)
    fprintf(stderr, "DEBUG - I/O thread finished\n");
#endif
  return NULL;
}

/****
 *
 * fill next chunk using worker's pre-allocated buffer (streaming)
 *
 ****/

int fill_next_chunk(chunk_dispatcher_t *dispatcher, worker_data_t *worker) {
  chunk_t *chunk = worker->chunk;
  char *buffer = worker->chunk_buffer;
  size_t available_space, bytes_to_read, bytes_read;
  char *last_newline;
  off_t read_start_offset;
  unsigned int start_line_number, lines_in_chunk;
  time_t current_time;
  
  if (dispatcher == NULL || worker == NULL) return FALSE;
  
  pthread_mutex_lock(&dispatcher->file_mutex);
  
  /* Check if we've reached end of file and no carry forward data */
  if (dispatcher->current_offset >= dispatcher->file_size && 
      dispatcher->carry_forward_size == 0) {
    pthread_mutex_unlock(&dispatcher->file_mutex);
    return FALSE;
  }
  
  /* Progress reporting handled by I/O thread - don't duplicate here */
  current_time = time(NULL);
  
  start_line_number = dispatcher->current_line_number;
  read_start_offset = dispatcher->current_offset;
  
  /* Start with carry forward data if any */
  size_t buffer_pos = 0;
  if (dispatcher->carry_forward_size > 0) {
    memcpy(buffer, dispatcher->carry_forward_buffer, dispatcher->carry_forward_size);
    buffer_pos = dispatcher->carry_forward_size;
    dispatcher->carry_forward_size = 0;
  }
  
  /* Calculate available space for new data */
  available_space = worker->max_chunk_size - buffer_pos;
  
  /* Don't read past end of file */
  bytes_to_read = available_space;
  if (dispatcher->current_offset + bytes_to_read > dispatcher->file_size) {
    bytes_to_read = dispatcher->file_size - dispatcher->current_offset;
  }
  
  /* Read new data if not at EOF */
  if (bytes_to_read > 0 && dispatcher->current_offset < dispatcher->file_size) {
    if (fseeko(dispatcher->file, dispatcher->current_offset, SEEK_SET) != 0) {
      fprintf(stderr, "ERR - Unable to seek to offset %ld\n", dispatcher->current_offset);
      pthread_mutex_unlock(&dispatcher->file_mutex);
      return FALSE;
    }
    
    bytes_read = fread(buffer + buffer_pos, 1, bytes_to_read, dispatcher->file);
    if (bytes_read > 0) {
      buffer_pos += bytes_read;
      dispatcher->current_offset += bytes_read;
    }
  }
  
  /* Null terminate for safety */
  buffer[buffer_pos] = '\0';
  
  /* Find the last complete line */
  last_newline = strrchr(buffer, '\n');
  
  if (last_newline == NULL) {
    /* No complete lines in buffer */
    if (dispatcher->current_offset >= dispatcher->file_size) {
      /* EOF reached, process incomplete line if any */
      if (buffer_pos > 0) {
        chunk->buffer_size = buffer_pos;
      } else {
        pthread_mutex_unlock(&dispatcher->file_mutex);
        return FALSE;  /* Nothing to process */
      }
    } else {
      /* No complete lines but more file data available - likely very long lines */
      /* Process what we have as an incomplete line to avoid thread starvation */
      if (buffer_pos > 0) {
        chunk->buffer_size = buffer_pos;
        /* Don't store as carry forward, just process the incomplete line */
      } else {
        pthread_mutex_unlock(&dispatcher->file_mutex);
        return FALSE;
      }
    }
  } else {
    /* Found complete lines */
    size_t complete_data_size = (last_newline - buffer) + 1;
    chunk->buffer_size = complete_data_size;
    
    /* Store remainder as carry forward for next chunk */
    size_t remainder_size = buffer_pos - complete_data_size;
    if (remainder_size > 0 && remainder_size <= dispatcher->carry_forward_capacity) {
      memcpy(dispatcher->carry_forward_buffer, buffer + complete_data_size, remainder_size);
      dispatcher->carry_forward_size = remainder_size;
    }
    
    /* Adjust current offset to account for carried forward data */
    dispatcher->current_offset -= remainder_size;
  }
  
  /* Count lines in this chunk */
  lines_in_chunk = 0;
  for (char *ptr = buffer; ptr < buffer + chunk->buffer_size; ptr++) {
    if (*ptr == '\n') {
      lines_in_chunk++;
    }
  }
  
  /* Fill chunk metadata */
  chunk->chunk_id = dispatcher->chunks_dispatched++;
  chunk->start_offset = read_start_offset;
  chunk->end_offset = dispatcher->current_offset;
  chunk->start_line_number = start_line_number;
  chunk->buffer = buffer;
  
  /* Update dispatcher state */
  dispatcher->current_line_number += lines_in_chunk;
  
  /* Ensure buffer is null terminated at actual data end */
  buffer[chunk->buffer_size] = '\0';
  
  pthread_mutex_unlock(&dispatcher->file_mutex);
  
#ifdef DEBUG
  if (config->debug >= 3) {
    fprintf(stderr, "DEBUG - Filled chunk %d for worker %d: %ld-%ld (%zu bytes, %u lines)\n",
            chunk->chunk_id, worker->thread_id, chunk->start_offset, chunk->end_offset, 
            chunk->buffer_size, lines_in_chunk);
  }
#endif
  
  return TRUE;
}

/****
 *
 * process a chunk of data
 *
 ****/

int process_chunk(worker_data_t *worker) {
  chunk_t *chunk = worker->chunk;
  char *line_start = chunk->buffer;
  char *line_end;
  char line_buf[65536];
  int ret;
  char oBuf[4096];
  
  /* Initialize parser for this thread */
  initParser();
  
  worker->lines_processed = 0;
  worker->addresses_found = 0;
  
  /* Process lines in chunk */
  while ((line_end = strchr(line_start, '\n')) != NULL && !quit) {
    size_t line_len = line_end - line_start;
    
    if (line_len > 0 && line_len < sizeof(line_buf) - 1) {
      memcpy(line_buf, line_start, line_len);
      line_buf[line_len] = '\0';
      
      /* Parse the line */
      if ((ret = parseLine(line_buf)) > 0) {
        for (int i = 1; i < ret; i++) {
          getParsedField(oBuf, sizeof(oBuf), i);
          
          if ((oBuf[0] == 'i') || (oBuf[0] == 'I') || (oBuf[0] == 'm')) {
            /* Send address to hash management thread */
            /* Strip parser prefix - hash functions should only use clean IP/MAC addresses */
            const char *clean_address = oBuf + 1;  /* Skip 'i', 'I', or 'm' prefix */
            
            /* Line number: chunk start + carry-forward lines + lines processed by this worker */
            unsigned int absolute_line = chunk->start_line_number + chunk->carry_forward_lines + worker->lines_processed;
            if (buffer_address_local(worker, clean_address, absolute_line, i)) {
              worker->addresses_found++;
            }
          }
        }
      }
      
      worker->lines_processed++;
    }
    
    line_start = line_end + 1;
  }
  
#ifdef DEBUG
  if (config->debug >= 2) {
    fprintf(stderr, "DEBUG - Thread %d: Processed %u lines, found %u unique addresses\n",
            worker->thread_id, worker->lines_processed, worker->addresses_found);
  }
#endif
  
  /* Flush any remaining addresses in local buffer */
  flush_local_buffer(worker);
  
  /* Clean up thread-local parser resources */
  deInitParser();
  
  return TRUE;
}

/****
 *
 * dedicated hash management thread (consumer)
 *
 ****/

void *hash_thread(void *arg) {
  parallel_context_t *ctx = (parallel_context_t *)arg;
  thread_pool_t *pool = ctx->pool;
  struct hash_s *hash = ctx->global_hash;
  hash_operation_entry_t *operation;
  unsigned int addresses_processed = 0;
  unsigned int new_addresses = 0;
  unsigned int updated_addresses = 0;
  time_t last_report_time = time(NULL);
  
  /* Only check hash growth every N inserts to reduce overhead */
  unsigned int new_addresses_since_check = 0;
  const unsigned int HASH_GROWTH_CHECK_INTERVAL = 4096;  /* Check every 4K new addresses */
  
#ifdef DEBUG
  if (config->debug >= 2)
    fprintf(stderr, "DEBUG - Hash management thread started\n");
#endif
  
  while (!pool->shutdown && !quit) {
    operation = dequeue_hash_operation(pool->address_queue);
    
    if (operation == NULL) {
      /* Check if all parser threads are done */
      if (pool->address_queue->active_producers == 0) {
        break;
      }
      continue;
    }
    
    /* Process hash operation based on type */
    struct hashRec_s *tmpRec;
    metaData_t *tmpMd;
    struct Address_s *tmpAddr;
    
    if (operation->op_type == HASH_OP_NEW_ADDRESS) {
      /* NEW ADDRESS: Worker determined this is new, but hash thread must always check for duplicates */
      tmpRec = getHashRecord(hash, operation->address);
      
      if (tmpRec != NULL) {
        /* RACE CONDITION DETECTED: Worker's hash lookup was stale */
#ifdef DEBUG
        if (config->debug >= 2) {
          fprintf(stderr, "DEBUG - Worker %d requested new address [%s] but it already exists in hash (race condition)\n", 
                  operation->worker_id, operation->address);
        }
#endif
        
        /* Treat as existing address update instead */
        tmpMd = (metaData_t *)tmpRec->data;
        
        if (tmpMd != NULL) {
          /* Get the requesting thread's location array */
          location_array_t *thread_array = get_thread_location_array(tmpMd, operation->worker_id);
          if (thread_array != NULL) {
            /* Add location to the requesting thread's array */
            if (!add_location_atomic(thread_array, operation->line_number, operation->field_offset)) {
              /* Array full, grow it */
              size_t current_capacity = thread_array->capacity;
              size_t new_capacity;
              
              if (current_capacity >= 1048576) {
                new_capacity = current_capacity + (current_capacity / 4);
              } else {
                new_capacity = current_capacity * 2;
              }
              
              if (grow_location_array(thread_array, new_capacity)) {
                if (!add_location_atomic(thread_array, operation->line_number, operation->field_offset)) {
#ifdef DEBUG
                  if (config->debug >= 1) {
                    fprintf(stderr, "DEBUG - Failed to add location after growing in hash thread race condition\n");
                  }
#endif
                }
              }
            }
            
            /* Update counts */
            tmpMd->thread_data[operation->worker_id].count++;
            tmpMd->total_count++;
            updated_addresses++;
          }
        }
        continue;  /* Skip the "new address" insertion */
      }
      
      /* Truly new address - create per-thread metadata structure */
      tmpMd = create_metadata(pool->num_workers);
      if (tmpMd == NULL) {
        fprintf(stderr, "ERR - Unable to create per-thread metadata in hash thread, aborting\n");
        abort();
      }
      
      /* Get the requesting thread's location array */
      location_array_t *thread_array = get_thread_location_array(tmpMd, operation->worker_id);
      if (thread_array == NULL) {
        fprintf(stderr, "ERR - Unable to get thread location array in hash thread, aborting\n");
        abort();
      }
      
      /* Add first location to the requesting thread's array */
      if (!add_location_atomic(thread_array, operation->line_number, operation->field_offset)) {
        fprintf(stderr, "ERR - Failed to add first location in hash thread, aborting\n");
        abort();
      }
      
      /* Update counts */
      tmpMd->thread_data[operation->worker_id].count = 1;
      tmpMd->total_count = 1;
      
      /* Add to the hash */
      addUniqueHashRec(hash, operation->address, strlen(operation->address) + 1, tmpMd);
      new_addresses++;
      new_addresses_since_check++;
      
      /* Only check hash growth periodically to reduce overhead */
      if (new_addresses_since_check >= HASH_GROWTH_CHECK_INTERVAL) {
        new_addresses_since_check = 0;
        
        /* Rebalance the hash if it gets too full */
        if (((float)hash->totalRecords / (float)hash->size) > 0.8) {
          if (hash->size >= MAX_HASH_SIZE) {
            fprintf(stderr, "WARNING - Hash table at maximum size (%d), performance may degrade\n", MAX_HASH_SIZE);
          } else if (hash->totalRecords >= MAX_HASH_ENTRIES) {
            fprintf(stderr, "ERR - Maximum number of hash entries reached (%d), aborting\n", MAX_HASH_ENTRIES);
            abort();
          } else {
            /* Growing hash with %d unique entries */
#ifdef DEBUG
            if (config->debug >= 2)
              fprintf(stderr, "DEBUG - Growing hash table from %d to larger size (%d unique entries)\n", 
                      hash->size, hash->totalRecords);
#endif
            
            /* Acquire write lock for hash growth */
            pthread_rwlock_wrlock(&ctx->hash_rwlock);
            hash = dyGrowHash(hash);
            ctx->global_hash = hash;  /* Update the context pointer */
            pthread_rwlock_unlock(&ctx->hash_rwlock);
          }
        }
      }
      
    } /* No HASH_OP_UPDATE_COUNT needed - workers handle their own arrays */
    
    addresses_processed++;
    
    /* Progress reporting every 60 seconds */
    time_t current_time = time(NULL);
    if (current_time - last_report_time >= 60) {
#ifdef DEBUG
      if (config->debug >= 2)
        fprintf(stderr, "DEBUG - Hash thread: %u total (%u new, %u updates), %u unique entries\n", 
                addresses_processed, new_addresses, updated_addresses, hash->totalRecords);
#endif
      last_report_time = current_time;
    }
  }
  
  /* Final hash size check in case we need one more grow */
  if (((float)hash->totalRecords / (float)hash->size) > 0.8) {
    if (hash->size < MAX_HASH_SIZE && hash->totalRecords < MAX_HASH_ENTRIES) {
#ifdef DEBUG
      if (config->debug >= 2)
        fprintf(stderr, "DEBUG - Final hash table grow from %d to larger size (%d unique entries)\n", 
                hash->size, hash->totalRecords);
#endif
      
      /* Acquire write lock for final hash growth */
      pthread_rwlock_wrlock(&ctx->hash_rwlock);
      hash = dyGrowHash(hash);
      ctx->global_hash = hash;
      pthread_rwlock_unlock(&ctx->hash_rwlock);
    }
  }
  
#ifdef DEBUG
  if (config->debug >= 2)
    fprintf(stderr, "DEBUG - Hash management thread finished: %u total processed (%u new, %u updates), %u unique entries\n",
            addresses_processed, new_addresses, updated_addresses, hash->totalRecords);
#endif
  
  return NULL;
}

/****
 *
 * worker thread function
 *
 ****/

void *worker_thread(void *arg) {
  worker_data_t *worker = (worker_data_t *)arg;
  thread_pool_t *pool = worker->pool;
  int chunks_processed = 0;
  
#ifdef DEBUG
  if (config->debug >= 1) {
    fprintf(stderr, "DEBUG - Worker thread %d starting\n", worker->thread_id);
  }
#endif
  
  while (!quit && !pool->shutdown) {
    /* Get next chunk from queue (blocks until available) */
    chunk_t *chunk = dequeue_chunk(pool->chunk_queue);
    
    if (chunk == NULL) {
      /* No more chunks - I/O thread finished */
      break;
    }
    
    /* Copy chunk data to worker's pre-allocated buffer */
    if (chunk->buffer_size <= worker->max_chunk_size) {
      memcpy(worker->chunk_buffer, chunk->buffer, chunk->buffer_size);
      worker->chunk_buffer[chunk->buffer_size] = '\0';
      
      /* Update worker's chunk metadata to point to local buffer */
      worker->chunk->chunk_id = chunk->chunk_id;
      worker->chunk->start_offset = chunk->start_offset;
      worker->chunk->end_offset = chunk->end_offset;
      worker->chunk->buffer_size = chunk->buffer_size;
      worker->chunk->start_line_number = chunk->start_line_number;
      worker->chunk->carry_forward_lines = chunk->carry_forward_lines;
      worker->chunk->buffer = worker->chunk_buffer;
      
      /* Free the original chunk buffer (we copied the data) */
      XFREE(chunk->buffer);
      XFREE(chunk);
      
      /* Mark as active worker */
      pthread_mutex_lock(&pool->pool_mutex);
      worker->status = 1;  /* working */
      pool->active_workers++;
      pthread_mutex_unlock(&pool->pool_mutex);
      
      /* Process the chunk */
      if (process_chunk(worker) == FAILED) {
        worker->status = -1;  /* error */
      } else {
        worker->status = 2;  /* done */
        chunks_processed++;
      }
      
      /* Mark completion */
      pthread_mutex_lock(&pool->pool_mutex);
      pool->active_workers--;
      pthread_cond_signal(&pool->work_done);
      pthread_mutex_unlock(&pool->pool_mutex);
      
    } else {
      /* Chunk too large - skip it */
      fprintf(stderr, "WARN - Worker %d: Chunk %d too large (%zu bytes), skipping\n",
              worker->thread_id, chunk->chunk_id, chunk->buffer_size);
      XFREE(chunk->buffer);
      XFREE(chunk);
    }
  }
  
#ifdef DEBUG
  if (config->debug >= 1) {
    fprintf(stderr, "DEBUG - Worker thread %d finished (processed %d chunks)\n", 
            worker->thread_id, chunks_processed);
  }
#endif
  
  /* Decrement active producers count for address queue */
  pthread_mutex_lock(&pool->address_queue->queue_mutex);
  pool->address_queue->active_producers--;
  /* Wake up hash thread if we were the last producer */
  if (pool->address_queue->active_producers == 0) {
    pthread_cond_signal(&pool->address_queue->not_empty);
  }
  pthread_mutex_unlock(&pool->address_queue->queue_mutex);
  
  return NULL;
}


/****
 *
 * merge local hash table into global
 *
 ****/

int merge_hash_tables(struct hash_s *global, struct hash_s *local) {
  uint32_t i, thread_id;
  struct hashRec_s *localRec, *globalRec, *nextLocalRec;
  metaData_t *localMd, *globalMd;
  location_array_t *local_array, *global_array;
  
  if (global == NULL || local == NULL) {
    return FAILED;
  }
  
  /* Iterate through all buckets in local hash */
  for (i = 0; i < local->size; i++) {
    localRec = local->buckets[i];
    
    while (localRec != NULL) {
      nextLocalRec = localRec->next;  /* Save next before we potentially modify the chain */
      
      if (localRec->data != NULL) {
        localMd = (metaData_t *)localRec->data;
        
        /* Check if this address exists in global hash */
        globalRec = getHashRecord(global, localRec->keyString);
        
        if (globalRec == NULL) {
          /* New address - transfer entire per-thread metadata to global hash */
          globalMd = localMd;  /* Transfer ownership */
          localRec->data = NULL;  /* Clear local ownership */
          
          /* Add to global hash */
          addUniqueHashRec(global, localRec->keyString, localRec->keyLen, globalMd);
        } else {
          /* Address exists in global - merge per-thread data */
          if (globalRec->data != NULL) {
            globalMd = (metaData_t *)globalRec->data;
            
            /* Update total count */
            globalMd->total_count += localMd->total_count;
            
            /* Merge per-thread data: find threads that have data in local */
            for (thread_id = 0; thread_id < localMd->max_threads; thread_id++) {
              if (localMd->thread_data[thread_id].locations != NULL) {
                /* This thread has data in local hash, merge into global */
                
                /* Ensure global has enough thread slots */
                if (thread_id >= globalMd->max_threads) {
                  /* Global needs to expand thread capacity - for now skip this case */
                  free_location_array(localMd->thread_data[thread_id].locations);
                  continue;
                }
                
                local_array = localMd->thread_data[thread_id].locations;
                global_array = globalMd->thread_data[thread_id].locations;
                
                if (global_array == NULL) {
                  /* Global thread has no data yet - transfer local thread's array */
                  globalMd->thread_data[thread_id].locations = local_array;
                  globalMd->thread_data[thread_id].count += localMd->thread_data[thread_id].count;
                  localMd->thread_data[thread_id].locations = NULL;  /* Clear local ownership */
                } else {
                  /* Both have data - for now, just keep the larger array */
                  if (local_array->count > global_array->count) {
                    free_location_array(global_array);
                    globalMd->thread_data[thread_id].locations = local_array;
                    globalMd->thread_data[thread_id].count = localMd->thread_data[thread_id].count;
                    localMd->thread_data[thread_id].locations = NULL;  /* Clear local ownership */
                  } else {
                    free_location_array(local_array);
                  }
                }
              }
            }
            
            /* Free local metadata structure (thread arrays were transferred or freed) */
            free_metadata(localMd);
            localRec->data = NULL;  /* Clear pointer to prevent double free */
          }
        }
      }
      
      localRec = nextLocalRec;
    }
    
    /* Clear the bucket to prevent hash cleanup from trying to free transferred data */
    local->buckets[i] = NULL;
  }
  
  return TRUE;
}

/****
 *
 * process file using parallel threads
 *
 ****/

int process_file_parallel(parallel_context_t *ctx) {
  int result = TRUE;
  
  /* Start dedicated I/O thread for producer-consumer pattern */
#ifdef DEBUG
  if (config->debug >= 2)
    fprintf(stderr, "DEBUG - Starting I/O thread...\n");
#endif
  if (pthread_create(&ctx->pool->io_thread, NULL, io_thread, ctx->pool) != 0) {
    fprintf(stderr, "ERR - Failed to create I/O thread\n");
    return FAILED;
  }
  ctx->pool->io_thread_created = 1;
  
  /* Start hash management thread */
#ifdef DEBUG
  if (config->debug >= 2)
    fprintf(stderr, "DEBUG - Starting hash management thread...\n");
#endif
  if (pthread_create(&ctx->pool->hash_thread, NULL, hash_thread, ctx) != 0) {
    fprintf(stderr, "ERR - Failed to create hash management thread\n");
    result = FAILED;
  } else {
    ctx->pool->hash_thread_created = 1;
  }
  
  
  /* Set active producers count for address queue */
  if (result == TRUE) {
    ctx->pool->address_queue->active_producers = ctx->pool->num_workers;
  }
  
  /* Start worker threads - they will consume chunks from queue */
#ifdef DEBUG
  if (config->debug >= 2)
    fprintf(stderr, "DEBUG - Starting %d worker threads...\n", ctx->pool->num_workers);
#endif
  for (int i = 0; i < ctx->pool->num_workers; i++) {
    if (pthread_create(&ctx->pool->workers[i].thread, NULL, 
                       worker_thread, &ctx->pool->workers[i]) != 0) {
      fprintf(stderr, "ERR - Failed to create worker thread %d\n", i);
      result = FAILED;
      break;
    } else {
#ifdef DEBUG
      if (config->debug >= 2)
        fprintf(stderr, "DEBUG - Created worker thread %d\n", i);
#endif
    }
  }
  
  if (result == FAILED) {
    /* Signal shutdown if thread creation failed */
    pthread_mutex_lock(&ctx->pool->pool_mutex);
    ctx->pool->shutdown = 1;
    if (ctx->pool->chunk_queue) {
      pthread_mutex_lock(&ctx->pool->chunk_queue->queue_mutex);
      ctx->pool->chunk_queue->finished = 1;
      pthread_cond_broadcast(&ctx->pool->chunk_queue->not_empty);
      pthread_mutex_unlock(&ctx->pool->chunk_queue->queue_mutex);
    }
    if (ctx->pool->address_queue) {
      pthread_mutex_lock(&ctx->pool->address_queue->queue_mutex);
      ctx->pool->address_queue->finished = 1;
      ctx->pool->address_queue->active_producers = 0;
      pthread_cond_broadcast(&ctx->pool->address_queue->not_empty);
      pthread_mutex_unlock(&ctx->pool->address_queue->queue_mutex);
    }
    pthread_mutex_unlock(&ctx->pool->pool_mutex);
  } else {
#ifdef DEBUG
    if (config->debug >= 2)
      fprintf(stderr, "DEBUG - Processing file with producer-consumer pattern (1 I/O + %d workers + 1 hash)...\n", 
              ctx->pool->num_workers);
#endif
  }
  
  /* Initialize line counting for progress reporting */
  ctx->lines_processed_this_minute = 0;
  ctx->last_report_time = time(NULL);
  
  /* Wait for I/O thread to finish (produces all chunks) */
  if (ctx->pool->io_thread_created) {
    pthread_join(ctx->pool->io_thread, NULL);
    ctx->pool->io_thread_created = 0; /* Mark as joined */
    ctx->pool->io_thread = 0; /* Clear handle */
  }
  
  /* Wait for all worker threads to finish processing */
  for (int i = 0; i < ctx->pool->num_workers; i++) {
    if (ctx->pool->workers[i].thread) {
      pthread_join(ctx->pool->workers[i].thread, NULL);
    }
  }
  
#ifdef DEBUG
  if (config->debug >= 2)
    fprintf(stderr, "DEBUG - File processing complete.\n");
#endif
  
  /* Signal shutdown */
  pthread_mutex_lock(&ctx->pool->pool_mutex);
  ctx->pool->shutdown = 1;
  /* Signal via chunk queue instead of work_ready */
  pthread_mutex_unlock(&ctx->pool->pool_mutex);
  
  /* Wait for worker threads to finish */
  for (int i = 0; i < ctx->pool->num_workers; i++) {
    if (ctx->pool->workers[i].thread) {
      pthread_join(ctx->pool->workers[i].thread, NULL);
      ctx->pool->workers[i].thread = 0; /* Clear handle to prevent double join */
    }
  }
  
#ifdef DEBUG
  if (config->debug >= 2)
    fprintf(stderr, "DEBUG - All worker threads finished. Waiting for hash thread to complete...\n");
#endif
  
  /* Wait for hash thread to finish processing all addresses */
  if (ctx->pool->hash_thread_created) {
    pthread_join(ctx->pool->hash_thread, NULL);
    ctx->pool->hash_thread_created = 0;
    ctx->pool->hash_thread = 0; /* Clear handle */
  }
  
  
#ifdef DEBUG
  if (config->debug >= 2)
    fprintf(stderr, "DEBUG - Parallel processing complete.\n");
#endif
  return result;
}