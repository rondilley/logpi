/*****
 *
 * Description: Log Pseudo Templater Headers
 * 
 * Copyright (c) 2008-2025, Ron Dilley
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

#ifndef LOGPI_DOT_H
#define LOGPI_DOT_H

/****
 *
 * defines
 *
 ****/

#define LINEBUF_SIZE 4096
#define MAX_HASH_SIZE 1000000  /* Maximum hash table size to prevent memory exhaustion */
#define MAX_HASH_ENTRIES 10000000  /* Maximum total entries to prevent DoS */

/****
 *
 * includes
 *
 ****/

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include "../include/sysdep.h"

#ifndef __SYSDEP_H__
# error something is messed up
#endif

#include "../include/common.h"
#include "util.h"
#include "mem.h"
#include "parser.h"
#include "hash.h"
#include "bintree.h"
#include "match.h"

/****
 *
 * consts & enums
 *
 ****/

/****
 *
 * typedefs & structs
 *
 ****/

/* Location entry for growable array - optimized for memory efficiency */
typedef struct {
  size_t line;          /* Line number (8 bytes on 64-bit) */
  uint16_t offset;      /* Field position (2 bytes, supports up to 65535 fields) */
} location_entry_t;

/* Growable array for storing address locations */
typedef struct {
  location_entry_t *entries;    /* Array of line:offset pairs */
  size_t count;                 /* Number of locations stored */
  size_t capacity;              /* Current array capacity */
  size_t reserved_count;        /* Thread-safe reservation counter */
  pthread_mutex_t mutex;        /* Mutex for thread-safe operations */
} location_array_t;

/* Per-thread location data for an IP/MAC address */
typedef struct {
  location_array_t *locations;  /* This thread's locations for this address */
  size_t count;                 /* This thread's count for this address */
} thread_location_data_t;

/* Metadata structure - supports per-thread location arrays */
typedef struct {
  size_t total_count;           /* Total occurrences across all threads */
  int max_threads;              /* Maximum number of threads */
  thread_location_data_t *thread_data;  /* Array of per-thread data */
} metaData_t;

/* Legacy struct for compatibility (will be phased out) */
struct Address_s {
  size_t line;
  size_t offset;
  struct Address_s *next;
};

/****
 *
 * function prototypes
 *
 ****/

/* Growable array functions */
location_array_t* create_location_array(size_t initial_capacity);
void free_location_array(location_array_t *array);
int add_location_atomic(location_array_t *array, size_t line, uint16_t offset);
int grow_location_array(location_array_t *array, size_t new_capacity);

/* Per-thread metadata functions */
metaData_t* create_metadata(int max_threads);
void free_metadata(metaData_t *metadata);
location_array_t* get_thread_location_array(metaData_t *metadata, int thread_id);

/* Address sorting for index output */
typedef struct address_for_sorting_s {
  char *address;                /* IP/MAC address string */
  size_t total_count;           /* Total occurrences */
  struct hashRec_s *hash_record; /* Pointer to original hash record */
} address_for_sorting_t;

int printAddress( const struct hashRec_s *hashRec );
void flushOutputBuffer(void);
int processFile( const char *fName );
int showAddresses( void );

#endif /* LOGPI_DOT_H */

