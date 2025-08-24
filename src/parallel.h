/*****
 *
 * Description: Parallel Processing Headers
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

#ifndef PARALLEL_DOT_H
#define PARALLEL_DOT_H

/****
 *
 * includes
 *
 ****/

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include <pthread.h>
#include <sched.h>
#include "../include/sysdep.h"
#include "../include/common.h"
#include "hash.h"
#include "logpi.h"

/****
 *
 * defines
 *
 ****/

#define DEFAULT_CHUNK_SIZE 134217728  /* 128MB chunks for better throughput */
#define MIN_CHUNK_SIZE 1048576        /* 1MB minimum */
#define MAX_THREADS 32                /* Maximum worker threads */
#define MAX_CHUNKS 500                /* Maximum number of chunks to prevent memory exhaustion */
#define MIN_FILE_SIZE_FOR_PARALLEL 104857600  /* 100MB minimum for parallel */

/****
 *
 * typedefs & structs
 *
 ****/

/* Chunk of file to process */
typedef struct chunk_s {
  off_t start_offset;
  off_t end_offset;
  char *buffer;
  size_t buffer_size;
  int chunk_id;
  unsigned int start_line_number;  /* Absolute line number where chunk starts */
  unsigned int carry_forward_lines; /* Lines from previous chunk at start of buffer */
  struct chunk_s *next;
} chunk_t;

/* Streaming chunk dispatcher */
typedef struct chunk_dispatcher_s {
  FILE *file;
  off_t current_offset;
  off_t file_size;
  unsigned int current_line_number;
  pthread_mutex_t file_mutex;
  int chunks_dispatched;
  int chunks_completed;
  size_t target_chunk_size;
  char *carry_forward_buffer;    /* Buffer for partial lines */
  size_t carry_forward_size;     /* Size of data in carry forward buffer */
  size_t carry_forward_capacity; /* Capacity of carry forward buffer */
  time_t start_time;
  time_t last_report_time;
} chunk_dispatcher_t;

/* Hash operation types for distributed architecture */
typedef enum hash_operation_e {
  HASH_OP_NEW_ADDRESS,     /* New address - needs hash insertion */
  HASH_OP_UPDATE_COUNT     /* Existing address - needs metadata update */
} hash_operation_t;

/* Hash operation entry for hash thread communication */
typedef struct hash_operation_entry_s {
  hash_operation_t op_type;      /* Type of operation to perform */
  char address[64];              /* Network address string (IPv4/IPv6/MAC) */
  unsigned int line_number;      /* Line number in file */
  uint16_t field_offset;         /* Field position in line (2 bytes, max 65535) */
  struct hashRec_s *hash_record; /* For updates: pointer to existing hash record */
  int worker_id;                 /* Worker thread ID for debugging */
} hash_operation_entry_t;

/* Legacy typedef for compatibility during transition */
typedef hash_operation_entry_t address_entry_t;

/* Worker thread data */
typedef struct worker_data_s {
  int thread_id;
  chunk_t *chunk;               /* Pre-allocated chunk structure */
  char *chunk_buffer;           /* Pre-allocated buffer for chunk data */
  size_t max_chunk_size;        /* Maximum chunk buffer size */
  unsigned int lines_processed;
  unsigned int addresses_found;
  int status;  /* 0=idle, 1=working, 2=done, -1=error */
  pthread_t thread;
  struct thread_pool_s *pool;  /* Back pointer to pool */
  
  /* Local hash operation buffer for batching */
  hash_operation_entry_t *local_buffer;
  int local_buffer_size;
  int local_buffer_count;
  int local_buffer_capacity;
} worker_data_t;

/* Chunk queue for producer-consumer */
typedef struct chunk_queue_s {
  chunk_t **chunks;           /* Array of chunk pointers */
  int capacity;               /* Maximum chunks in queue */
  int count;                  /* Current chunks in queue */
  int head;                   /* Next chunk to consume */
  int tail;                   /* Next position to produce */
  pthread_mutex_t queue_mutex;
  pthread_cond_t not_empty;   /* Signal when chunks available */
  pthread_cond_t not_full;    /* Signal when space available */
  int finished;               /* I/O thread finished producing */
} chunk_queue_t;

/* Hash operation queue for worker->hash communication */
typedef struct address_queue_s {
  hash_operation_entry_t *entries;   /* Array of hash operation entries */
  int capacity;               /* Maximum entries in queue */
  int count;                  /* Current entries in queue */
  int head;                   /* Next entry to consume */
  int tail;                   /* Next position to produce */
  pthread_mutex_t queue_mutex;
  pthread_cond_t not_empty;   /* Signal when entries available */
  pthread_cond_t not_full;    /* Signal when space available */
  int finished;               /* All parser threads finished */
  int active_producers;       /* Number of active parser threads */
} address_queue_t;

/* Thread pool management */
typedef struct thread_pool_s {
  worker_data_t *workers;
  int num_workers;
  int active_workers;
  pthread_mutex_t pool_mutex;
  pthread_cond_t work_done;
  chunk_queue_t *chunk_queue;     /* Queue for producer-consumer */
  address_queue_t *address_queue; /* Queue for parser->hash communication */
  chunk_dispatcher_t *dispatcher; /* I/O thread context */
  pthread_t io_thread;            /* Dedicated I/O thread */
  pthread_t hash_thread;          /* Dedicated hash management thread */
  pthread_t monitor_thread;       /* Progress monitoring thread */
  int io_thread_created;          /* Flag: 1 if I/O thread was created */
  int hash_thread_created;        /* Flag: 1 if hash thread was created */
  int monitor_thread_created;     /* Flag: 1 if monitor thread was created */
  int shutdown;
  struct parallel_context_s *ctx; /* Back pointer to context for accessing global hash */
} thread_pool_t;

/* File processing context */
typedef struct parallel_context_s {
  const char *filename;
  FILE *file;
  off_t file_size;
  thread_pool_t *pool;
  struct hash_s *global_hash;
  pthread_rwlock_t hash_rwlock;  /* Protects hash table during growth operations */
  size_t chunk_size;
  
  /* Simple line counting for progress reporting */
  volatile unsigned long lines_processed_this_minute;  /* Atomic counter for lines */
  time_t last_report_time;                             /* Last time we reported */
} parallel_context_t;

/****
 *
 * function prototypes
 *
 ****/

int get_available_cores(void);
int should_use_parallel(off_t file_size, int available_cores);
parallel_context_t *init_parallel_context(const char *filename, FILE *file, struct hash_s *hash);
void free_parallel_context(parallel_context_t *ctx);
thread_pool_t *create_thread_pool(int num_threads);
void destroy_thread_pool(thread_pool_t *pool);
chunk_dispatcher_t *init_chunk_dispatcher(FILE *file, off_t file_size, size_t chunk_size);
void free_chunk_dispatcher(chunk_dispatcher_t *dispatcher);
chunk_queue_t *create_chunk_queue(int capacity);
void destroy_chunk_queue(chunk_queue_t *queue);
int enqueue_chunk(chunk_queue_t *queue, chunk_t *chunk);
chunk_t *dequeue_chunk(chunk_queue_t *queue);
address_queue_t *create_address_queue(int capacity);
void destroy_address_queue(address_queue_t *queue);
int enqueue_hash_operation(address_queue_t *queue, hash_operation_t op_type, const char *address, unsigned int line_number, uint16_t field_offset, struct hashRec_s *hash_record, int worker_id);
hash_operation_entry_t *dequeue_hash_operation(address_queue_t *queue);
/* Legacy function for compatibility */
int enqueue_address(address_queue_t *queue, const char *address, unsigned int line_number, uint16_t field_offset, int worker_id);
address_entry_t *dequeue_address(address_queue_t *queue);
void *io_thread(void *arg);
void *worker_thread(void *arg);
void *hash_thread(void *arg);
void *monitor_thread(void *arg);
int process_chunk(worker_data_t *worker);
int merge_hash_tables(struct hash_s *global, struct hash_s *local);
int process_file_parallel(parallel_context_t *ctx);
off_t get_file_size(FILE *file);
int find_line_boundary(FILE *file, off_t offset);
int has_pending_new_address_in_buffer(worker_data_t *worker, const char *address);
int flush_local_buffer_immediate(worker_data_t *worker);

#endif /* PARALLEL_DOT_H */