/*****
 *
 * Description: Log Pseudo Indexer Functions
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

/****
 *
 * includes
 *
 ****/

#include "logpi.h"
#include "parallel.h"

/****
 *
 * local variables
 *
 ****/

/****
 *
 * global variables
 *
 ****/

/* hashes */
struct hash_s *addrHash = NULL;

/****
 *
 * external variables
 *
 ****/

extern int errno;
extern char **environ;
extern Config_t *config;
extern int quit;
extern int reload;

/****
 *
 * functions
 *
 ****/

/****
 *
 * create location array
 *
 ****/

location_array_t* create_location_array(size_t initial_capacity) {
  location_array_t *array;
  
  if (initial_capacity < 64) initial_capacity = 64;  /* Minimum size */
  
  array = (location_array_t *)XMALLOC(sizeof(location_array_t));
  if (array == NULL) {
    fprintf(stderr, "ERR - Unable to allocate location array\n");
    return NULL;
  }
  
  array->entries = (location_entry_t *)XMALLOC(sizeof(location_entry_t) * initial_capacity);
  if (array->entries == NULL) {
    fprintf(stderr, "ERR - Unable to allocate location entries\n");
    XFREE(array);
    return NULL;
  }
  
  array->count = 0;
  array->capacity = initial_capacity;
  array->reserved_count = 0;
  
  /* Initialize mutex for thread-safe operations */
  if (pthread_mutex_init(&array->mutex, NULL) != 0) {
    fprintf(stderr, "ERR - Unable to initialize location array mutex\n");
    XFREE(array->entries);
    XFREE(array);
    return NULL;
  }
  
  return array;
}

/****
 *
 * free location array
 *
 ****/

void free_location_array(location_array_t *array) {
  if (array == NULL) return;
  
  /* Destroy the mutex */
  pthread_mutex_destroy(&array->mutex);
  
  if (array->entries) {
    XFREE(array->entries);
  }
  XFREE(array);
}

/****
 *
 * grow location array (called when capacity exceeded)
 *
 ****/

int grow_location_array(location_array_t *array, size_t new_capacity) {
  location_entry_t *new_entries;
  int result;
  
  if (array == NULL) {
    return FALSE;
  }
  
  /* Safety check for overflow - prevent ridiculously large allocations */
  if (new_capacity > SIZE_MAX / sizeof(location_entry_t) || 
      new_capacity < array->capacity) {
    fprintf(stderr, "ERR - Invalid location array capacity %zu (current: %zu)\n", 
            new_capacity, array->capacity);
    return FALSE;
  }
  
  if (new_capacity <= array->capacity) {
    return FALSE;
  }
  
  /* Lock the array for thread-safe resizing */
  pthread_mutex_lock(&array->mutex);
  
  /* Double-check capacity after acquiring lock (another thread might have grown it) */
  if (new_capacity <= array->capacity) {
    pthread_mutex_unlock(&array->mutex);
    return TRUE;  /* Already grown by another thread */
  }
  
  new_entries = (location_entry_t *)realloc(array->entries, 
                                          sizeof(location_entry_t) * new_capacity);
  if (new_entries == NULL) {
    fprintf(stderr, "ERR - Unable to grow location array to %zu entries (%zu MB)\n", 
            new_capacity, (new_capacity * sizeof(location_entry_t)) / 1048576);
    result = FALSE;
  } else {
    array->entries = new_entries;
    array->capacity = new_capacity;
    result = TRUE;
  }
  
  /* Unlock the array */
  pthread_mutex_unlock(&array->mutex);
  
  return result;
}

/****
 *
 * create metadata with per-thread arrays
 *
 ****/

metaData_t* create_metadata(int max_threads) {
  metaData_t *metadata;
  int i;
  
  metadata = (metaData_t *)XMALLOC(sizeof(metaData_t));
  if (metadata == NULL) {
    fprintf(stderr, "ERR - Unable to allocate metadata\n");
    return NULL;
  }
  
  metadata->total_count = 0;
  metadata->max_threads = max_threads;
  
  metadata->thread_data = (thread_location_data_t *)XMALLOC(sizeof(thread_location_data_t) * max_threads);
  if (metadata->thread_data == NULL) {
    fprintf(stderr, "ERR - Unable to allocate thread data array\n");
    XFREE(metadata);
    return NULL;
  }
  
  /* Initialize each thread's data */
  for (i = 0; i < max_threads; i++) {
    metadata->thread_data[i].locations = NULL;
    metadata->thread_data[i].count = 0;
  }
  
  return metadata;
}

/****
 *
 * free metadata and all thread arrays
 *
 ****/

void free_metadata(metaData_t *metadata) {
  int i;
  
  if (metadata == NULL) return;
  
  if (metadata->thread_data != NULL) {
    /* Free each thread's location array */
    for (i = 0; i < metadata->max_threads; i++) {
      if (metadata->thread_data[i].locations != NULL) {
        free_location_array(metadata->thread_data[i].locations);
      }
    }
    XFREE(metadata->thread_data);
  }
  
  XFREE(metadata);
}

/****
 *
 * get or create location array for specific thread
 *
 ****/

location_array_t* get_thread_location_array(metaData_t *metadata, int thread_id) {
  if (metadata == NULL || thread_id < 0 || thread_id >= metadata->max_threads) {
    return NULL;
  }
  
  /* Create array if it doesn't exist for this thread */
  if (metadata->thread_data[thread_id].locations == NULL) {
    metadata->thread_data[thread_id].locations = create_location_array(1024);
  }
  
  return metadata->thread_data[thread_id].locations;
}

/****
 *
 * add location atomically (thread-safe)
 *
 ****/

int add_location_atomic(location_array_t *array, size_t line, uint16_t offset) {
  size_t index;
  int result = TRUE;
  
  if (array == NULL || array->entries == NULL) return FALSE;
  
  /* Lock the array for thread-safe access */
  pthread_mutex_lock(&array->mutex);
  
  /* Check if array is full */
  if (array->count >= array->capacity) {
    result = FALSE;  /* Array is full, needs growing */
  } else {
    /* Reserve a slot and store the location */
    index = array->count;
    array->entries[index].line = line;
    array->entries[index].offset = offset;
    array->count++;
  }
  
  /* Unlock the array */
  pthread_mutex_unlock(&array->mutex);
  
  return result;
}

/****
 *
 * print all addr records in hash
 *
 ****/

/* Global mega-buffer for batched output */
static char mega_buffer[1048576];  /* 1MB buffer */
static int mega_buffer_pos = 0;

/* Forward declarations */
static struct Address_s* mergeAddresses(struct Address_s* left, struct Address_s* right);

/* Merge sort for linked list of Address_s structures */
static struct Address_s* mergeSortAddresses(struct Address_s* head) {
  if (head == NULL || head->next == NULL) {
    return head;
  }
  
  /* Split the list into two halves */
  struct Address_s* slow = head;
  struct Address_s* fast = head;
  struct Address_s* prev = NULL;
  
  while (fast != NULL && fast->next != NULL) {
    prev = slow;
    slow = slow->next;
    fast = fast->next->next;
  }
  
  prev->next = NULL;  /* Break the list */
  
  /* Recursively sort both halves */
  struct Address_s* left = mergeSortAddresses(head);
  struct Address_s* right = mergeSortAddresses(slow);
  
  /* Merge the sorted halves */
  return mergeAddresses(left, right);
}

static struct Address_s* mergeAddresses(struct Address_s* left, struct Address_s* right) {
  struct Address_s dummy;
  struct Address_s* tail = &dummy;
  dummy.next = NULL;
  
  while (left != NULL && right != NULL) {
    if (left->line <= right->line) {
      tail->next = left;
      left = left->next;
    } else {
      tail->next = right;
      right = right->next;
    }
    tail = tail->next;
  }
  
  /* Append remaining nodes */
  if (left != NULL) {
    tail->next = left;
  } else {
    tail->next = right;
  }
  
  return dummy.next;
}

/* Comparison function for qsort - sort by line number */
static int compare_locations(const void *a, const void *b) {
  const location_entry_t *loc_a = (const location_entry_t *)a;
  const location_entry_t *loc_b = (const location_entry_t *)b;
  
  if (loc_a->line < loc_b->line) return -1;
  if (loc_a->line > loc_b->line) return 1;
  return 0;  /* Equal line numbers */
}

/* Global array for collecting addresses for sorting */
static address_for_sorting_t *addresses_to_sort = NULL;
static size_t addresses_to_sort_count = 0;
static size_t addresses_to_sort_capacity = 0;

/* Comparison function for address sorting: frequency desc, then IP numerical */
static int compare_addresses_for_output(const void *a, const void *b) {
  const address_for_sorting_t *addr_a = (const address_for_sorting_t *)a;
  const address_for_sorting_t *addr_b = (const address_for_sorting_t *)b;
  
  /* Primary sort: by frequency (descending - most frequent first) */
  if (addr_a->total_count > addr_b->total_count) return -1;
  if (addr_a->total_count < addr_b->total_count) return 1;
  
  /* Secondary sort: by IP address (numerical) for tie-breaking */
  /* For now, use string comparison - could enhance with numerical IP parsing */
  return strcmp(addr_a->address, addr_b->address);
}

/* Collect address for sorting instead of printing immediately */
static int collectAddressForSorting(const struct hashRec_s *hashRec) {
  metaData_t *tmpMd;
  size_t total_count = 0;
  int i;

  if (hashRec->data != NULL) {
    tmpMd = (metaData_t *)hashRec->data;

    /* Calculate total count across all threads */
    for (i = 0; i < tmpMd->max_threads; i++) {
      if (tmpMd->thread_data[i].locations != NULL) {
        total_count += tmpMd->thread_data[i].count;
      }
    }

    /* Grow collection array if needed */
    if (addresses_to_sort_count >= addresses_to_sort_capacity) {
      size_t new_capacity = addresses_to_sort_capacity == 0 ? 1024 : addresses_to_sort_capacity * 2;
      address_for_sorting_t *new_array = (address_for_sorting_t *)realloc(addresses_to_sort, 
                                                                         sizeof(address_for_sorting_t) * new_capacity);
      if (new_array == NULL) {
        fprintf(stderr, "ERR - Unable to grow address collection array\n");
        return TRUE; /* Stop traversal */
      }
      addresses_to_sort = new_array;
      addresses_to_sort_capacity = new_capacity;
    }

    /* Store address info for later sorting */
    addresses_to_sort[addresses_to_sort_count].address = strdup(hashRec->keyString);
    addresses_to_sort[addresses_to_sort_count].total_count = total_count;
    addresses_to_sort[addresses_to_sort_count].hash_record = (struct hashRec_s *)hashRec;
    addresses_to_sort_count++;
  }

  return FALSE; /* Continue traversal */
}

/* K-way merge streaming output - no memory allocation needed */
static void stream_sorted_locations(FILE *output_stream, metaData_t *tmpMd) {
  size_t thread_indices[MAX_THREADS] = {0}; /* Current position in each thread's array */
  location_array_t *thread_arrays[MAX_THREADS];
  int active_threads = 0;
  int i;
  
  /* Initialize thread arrays and sort each one */
  for (i = 0; i < tmpMd->max_threads; i++) {
    if (tmpMd->thread_data[i].locations != NULL && tmpMd->thread_data[i].locations->count > 0) {
      thread_arrays[i] = tmpMd->thread_data[i].locations;
      /* Sort this thread's array in-place */
      qsort(thread_arrays[i]->entries, thread_arrays[i]->count, 
            sizeof(location_entry_t), compare_locations);
      active_threads++;
    } else {
      thread_arrays[i] = NULL;
    }
  }
  
  /* Stream output using k-way merge (simple linear scan for k=4) */
  while (active_threads > 0) {
    size_t min_line = SIZE_MAX;
    uint16_t min_offset = 0;
    int min_thread = -1;
    
    /* Find thread with minimum line number */
    for (i = 0; i < tmpMd->max_threads; i++) {
      if (thread_arrays[i] != NULL && thread_indices[i] < thread_arrays[i]->count) {
        location_entry_t *entry = &thread_arrays[i]->entries[thread_indices[i]];
        if (entry->line < min_line) {
          min_line = entry->line;
          min_offset = entry->offset;
          min_thread = i;
        }
      }
    }
    
    /* Output the minimum entry */
    if (min_thread >= 0) {
      fprintf(output_stream, ",%zu:%u", min_line + 1, min_offset);
      
      /* Advance the pointer for this thread */
      thread_indices[min_thread]++;
      
      /* Check if this thread is exhausted */
      if (thread_indices[min_thread] >= thread_arrays[min_thread]->count) {
        thread_arrays[min_thread] = NULL;
        active_threads--;
      }
    } else {
      break; /* Safety break - shouldn't happen */
    }
  }
}

int printAddress(const struct hashRec_s *hashRec) {
  metaData_t *tmpMd;
  FILE *output_stream;
  size_t total_count = 0;
  int i;

  if (hashRec->data != NULL) {
    tmpMd = (metaData_t *)hashRec->data;

#ifdef DEBUG
    if (config->debug >= 3)
      printf("DEBUG - Searching for [%s]\n", hashRec->keyString);
#endif

    output_stream = config->outFile_st ? config->outFile_st : stdout;

    /* Calculate total count across all threads */
    for (i = 0; i < tmpMd->max_threads; i++) {
      if (tmpMd->thread_data[i].locations != NULL) {
        total_count += tmpMd->thread_data[i].count;
      }
    }

    /* Write address and total count */
    fprintf(output_stream, "%s,%zu", hashRec->keyString, total_count);

    /* Stream sorted locations directly - no memory allocation needed! */
    if (total_count > 0) {
      stream_sorted_locations(output_stream, tmpMd);
    }

    /* Write newline */
    fprintf(output_stream, "\n");
    
    /* Free all thread metadata */
    free_metadata(tmpMd);
  }

  /* can use this later to interrupt traversing the hash */
  if (quit)
    return (TRUE);
  return (FALSE);
}

/* Flush any remaining buffered output */
void flushOutputBuffer(void) {
  FILE *output_stream = config->outFile_st ? config->outFile_st : stdout;
  fflush(output_stream);
}

/****
 *
 * process file
 *
 ****/

int processFile(const char *fName) {
  FILE *inFile = NULL, *outFile = NULL;
  gzFile gzInFile;
  char inBuf[65536];  /* Increased buffer size for better I/O performance */
  char outFileName[PATH_MAX];
  char patternBuf[4096];
  char oBuf[4096];
  char *foundPtr;
  PRIVATE int c = 0, i, ret;
  unsigned int totLineCount = 0, lineCount = 0, lineLen = 0,
               minLineLen = sizeof(inBuf), maxLineLen = 0, totLineLen = 0;
  unsigned int argCount = 0, totArgCount = 0, minArgCount = MAX_FIELD_POS,
               maxArgCount = 0;
  struct hashRec_s *tmpRec;
  metaData_t *tmpMd;
  struct Address_s *tmpAddr;
  struct Fields_s **curFieldPtr;
  int isGz = FALSE;

  /* Handle automatic .lpi file naming */
  if (config->auto_lpi_naming) {
    /* Generate output filename: input.ext -> input.ext.lpi */
    if (snprintf(outFileName, sizeof(outFileName), "%s.lpi", fName) >= sizeof(outFileName)) {
      fprintf(stderr, "ERR - Output filename too long for [%s]\n", fName);
      return (EXIT_FAILURE);
    }
    
    /* Validate the generated path for security */
    if (!is_path_safe(outFileName)) {
      fprintf(stderr, "ERR - Unsafe output file path [%s]\n", outFileName);
      return (EXIT_FAILURE);
    }
    
    /* Open the output file for this specific input file */
    if ((config->outFile_st = fopen(outFileName, "w")) == NULL) {
      fprintf(stderr, "ERR - Unable to open output file [%s]: %s\n", 
              outFileName, strerror(errno));
      return (EXIT_FAILURE);
    }
    
    fprintf(stderr, "Writing index to [%s]\n", outFileName);
  }

  /* initialize the hash if we need to */
  if (addrHash EQ NULL)
    addrHash = initHash(65536);  /* Start with 64K buckets for better performance */

  initParser();

  /* XXX need to add bzip2 */

  /* check to see if the file is compressed */
  if ((((foundPtr = strrchr(fName, '.')) != NULL)) &&
      (strncmp(foundPtr, ".gz", 3) EQ 0))
    isGz = TRUE;

  fprintf(stderr, "Opening [%s] for read\n", fName);
  
  /* Check if we should use parallel processing */
  int use_parallel = FALSE;
  parallel_context_t *parallel_ctx = NULL;
  
  if (!isGz && strcmp(fName, "-") != 0) {
    /* Open file to check size */
    FILE *testFile = NULL;
#ifdef HAVE_FOPEN64
    testFile = fopen64(fName, "r");
#else
    testFile = fopen(fName, "r");
#endif
    if (testFile != NULL) {
      off_t file_size = get_file_size(testFile);
      int cores = get_available_cores();
      
      if (!config->force_serial && should_use_parallel(file_size, cores)) {
        use_parallel = TRUE;
        fprintf(stderr, "Using parallel processing (%d threads) for large file (%ld MB)\n", 
                cores / 2, file_size / 1048576);
      } else if (config->force_serial) {
        fprintf(stderr, "Serial processing forced for large file (%ld MB)\n", 
                file_size / 1048576);
      }
      fclose(testFile);
    }
  }

  if (isGz) {
    /* gzip compressed */
    if ((gzInFile = gzopen(fName, "rb")) EQ NULL) {
      fprintf(stderr, "ERR - Unable to open file [%s] %d (%s)\n", fName, errno,
              strerror(errno));
      return (EXIT_FAILURE);
    }
  } else {
    if (strcmp(fName, "-") EQ 0) {
      inFile = stdin;
    } else {
#ifdef HAVE_FOPEN64
      if ((inFile = fopen64(fName, "r")) EQ NULL) {
#else
      if ((inFile = fopen(fName, "r")) EQ NULL) {
#endif
        fprintf(stderr, "ERR - Unable to open file [%s] %d (%s)\n", fName,
                errno, strerror(errno));
        return (EXIT_FAILURE);
      }
    }
  }
  
  /* Use parallel processing for large files */
  if (use_parallel && inFile != NULL && inFile != stdin) {
    parallel_ctx = init_parallel_context(fName, inFile, addrHash);
    if (parallel_ctx != NULL) {
      int result = process_file_parallel(parallel_ctx);
      
      
      /* Save updated hash pointer and update global before freeing context */
      struct hash_s *final_hash = parallel_ctx->global_hash;
      addrHash = final_hash;  /* Update global pointer */
      
      free_parallel_context(parallel_ctx);
      fclose(inFile);
      deInitParser();
      
      /* Close auto-generated output file */
      if (config->auto_lpi_naming && config->outFile_st) {
        /* Write addresses to this file in sorted order */
        if (final_hash != NULL) {
          /* Reset collection arrays */
          addresses_to_sort_count = 0;
          
          /* Collect all addresses for sorting */
          traverseHash(final_hash, collectAddressForSorting);
          
          /* Sort addresses by frequency (desc) then IP (asc) */
          if (addresses_to_sort_count > 0) {
            qsort(addresses_to_sort, addresses_to_sort_count, sizeof(address_for_sorting_t), compare_addresses_for_output);
            
            /* Output addresses in sorted order */
            for (size_t i = 0; i < addresses_to_sort_count; i++) {
              /* Print this address using the original printAddress logic */
              printAddress(addresses_to_sort[i].hash_record);
              
              /* Free the duplicated address string */
              free(addresses_to_sort[i].address);
            }
          }
          
          /* Clean up */
          if (addresses_to_sort != NULL) {
            free(addresses_to_sort);
            addresses_to_sort = NULL;
            addresses_to_sort_capacity = 0;
            addresses_to_sort_count = 0;
          }
          
          flushOutputBuffer();
          freeHash(final_hash);
          addrHash = NULL; /* Reset global for next file */
        }
        fclose(config->outFile_st);
        config->outFile_st = NULL;
      }
      
      return (result == TRUE) ? EXIT_SUCCESS : EXIT_FAILURE;
    } else {
      fprintf(stderr, "WARN - Failed to initialize parallel processing, falling back to sequential\n");
      use_parallel = FALSE;
    }
  }

  /* Optimization: Only check hash growth every N new addresses to reduce overhead */
  unsigned int new_addresses_since_check = 0;
  const unsigned int HASH_GROWTH_CHECK_INTERVAL = 4096;  /* Check every 4K new addresses */

  /* XXX should block read based on filesystem BS */
  /* XXX should switch to file offsets instead of line numbers, should speed up
   * the index searches */
  while (((isGz) ? gzgets(gzInFile, inBuf, sizeof(inBuf))
                 : fgets(inBuf, sizeof(inBuf), inFile)) != NULL &&
         !quit) {

    if (reload EQ TRUE) {
      fprintf(stderr, "Processed %d lines/min\n", lineCount);
#ifdef DEBUG
      if (config->debug) {
        fprintf(stderr, "Line length: min=%d, max=%d, avg=%2.0f\n", minLineLen,
                maxLineLen, (float)totLineLen / (float)lineCount);

        minLineLen = sizeof(inBuf);
        maxLineLen = 0;
        totLineLen = 0;
      }
#endif
      lineCount = 0;
      reload = FALSE;
    }

#ifdef DEBUG
    if (config->debug) {
      lineLen = strlen(inBuf);
      totLineLen += lineLen;
      if (lineLen < minLineLen)
        minLineLen = lineLen;
      else if (lineLen > maxLineLen)
        maxLineLen = lineLen;
    }
#endif

    if (config->debug >= 3)
      printf("DEBUG - Before [%s]", inBuf);

    if ((ret = parseLine(inBuf)) > 0) {

#ifdef DEBUG
      if (config->debug) {
        /* save arg count */
        totArgCount += ret;
        if (ret < minArgCount)
          minArgCount = ret;
        else if (ret > maxArgCount)
          maxArgCount = ret;
      }
#endif

      for (i = 1; i < ret; i++) {
        getParsedField(oBuf, sizeof(oBuf), i);
        if ((oBuf[0] EQ 'i') || (oBuf[0] EQ 'I') || (oBuf[0] EQ 'm')) {
          /* Strip parser prefix - hash functions should only use clean IP/MAC addresses */
          const char *clean_address = oBuf + 1;  /* Skip 'i', 'I', or 'm' prefix */
          
          if ((tmpRec = getHashRecord(addrHash, clean_address))
                  EQ NULL) { /* store line metadata */

            /* Create per-thread metadata (serial mode uses single thread 0) */
            tmpMd = create_metadata(1);  /* Serial mode = 1 thread */
            if (tmpMd == NULL) {
              fprintf(stderr, "ERR - Unable to create metadata, aborting\n");
              abort();
            }

            /* Get thread 0's location array (serial mode) */
            location_array_t *thread_array = get_thread_location_array(tmpMd, 0);
            if (thread_array == NULL) {
              fprintf(stderr, "ERR - Unable to get thread location array, aborting\n");
              abort();
            }
            
            /* Add first location to thread 0's array */
            if (!add_location_atomic(thread_array, totLineCount, i)) {
              fprintf(stderr, "ERR - Failed to add first location, aborting\n");
              abort();
            }
            
            /* Update counts */
            tmpMd->thread_data[0].count = 1;
            tmpMd->total_count = 1;

            /* add to the hash */
            addUniqueHashRec(addrHash, clean_address, strlen(clean_address) + 1, tmpMd);
            new_addresses_since_check++;

            /* Only check hash growth periodically to reduce overhead */
            if (new_addresses_since_check >= HASH_GROWTH_CHECK_INTERVAL) {
              new_addresses_since_check = 0;
              
              /* rebalance the hash if it gets too full, with limits */
              if (((float)addrHash->totalRecords / (float)addrHash->size) > 0.8) {
                if (addrHash->size >= MAX_HASH_SIZE) {
                  fprintf(stderr, "WARNING - Hash table at maximum size (%d), performance may degrade\n", MAX_HASH_SIZE);
                } else if (addrHash->totalRecords >= MAX_HASH_ENTRIES) {
                  fprintf(stderr, "ERR - Maximum number of hash entries reached (%d), aborting\n", MAX_HASH_ENTRIES);
                  abort();
                } else {
                  addrHash = dyGrowHash(addrHash);
                }
              }
            }
          } else {
            /* update the address counts */
            if (tmpRec->data != NULL) {
              tmpMd = (metaData_t *)tmpRec->data;
              
              /* Get thread 0's location array (serial mode) */
              location_array_t *thread_array = get_thread_location_array(tmpMd, 0);
              if (thread_array == NULL) {
                fprintf(stderr, "ERR - Unable to get thread location array for existing address\n");
                continue;
              }
              
              /* Add location to thread 0's array */
              if (!add_location_atomic(thread_array, totLineCount, i)) {
                /* Array is full, grow it directly */
                size_t current_capacity = thread_array->capacity;
                size_t new_capacity;
                
                if (current_capacity >= 1048576) {  /* 1M entries = 16MB */
                  /* Large array - grow conservatively */
                  new_capacity = current_capacity + (current_capacity / 4);  /* Grow by 25% */
                } else {
                  new_capacity = current_capacity * 2;  /* Normal doubling */
                }
                
                if (!grow_location_array(thread_array, new_capacity)) {
                  fprintf(stderr, "ERR - Failed to grow location array from %zu to %zu, aborting\n", 
                          current_capacity, new_capacity);
                  abort();
                }
                /* Try again after growing */
                if (!add_location_atomic(thread_array, totLineCount, i)) {
                  fprintf(stderr, "ERR - Failed to add location after growing, aborting\n");
                  abort();
                }
              }
              
              /* Update counts */
              tmpMd->thread_data[0].count++;
              tmpMd->total_count++;
            }
          }
        }
      }

      lineCount++;
      totLineCount++;
    }
  }

#ifdef DEBUG
  if (config->debug) {
    fprintf(stderr, "Line length: min=%d, max=%d, avg=%2.0f\n", minLineLen,
            maxLineLen, (float)totLineLen / (float)lineCount);

    minLineLen = sizeof(inBuf);
    maxLineLen = 0;
    totLineLen = 0;
  }
#endif

  if (inFile != stdin) {
    if (isGz)
      gzclose(gzInFile);
    else
      fclose(inFile);
  }

  deInitParser();

  /* For auto-naming, write addresses to file and close it */
  if (config->auto_lpi_naming && config->outFile_st) {
    /* Write addresses to this file in sorted order */
    if (addrHash != NULL) {
      /* Reset collection arrays */
      addresses_to_sort_count = 0;
      
      /* Collect all addresses for sorting */
      traverseHash(addrHash, collectAddressForSorting);
      
      /* Sort addresses by frequency (desc) then IP (asc) */
      if (addresses_to_sort_count > 0) {
        qsort(addresses_to_sort, addresses_to_sort_count, sizeof(address_for_sorting_t), compare_addresses_for_output);
        
        /* Output addresses in sorted order */
        for (size_t i = 0; i < addresses_to_sort_count; i++) {
          /* Print this address using the original printAddress logic */
          printAddress(addresses_to_sort[i].hash_record);
          
          /* Free the duplicated address string */
          free(addresses_to_sort[i].address);
        }
      }
      
      /* Clean up */
      if (addresses_to_sort != NULL) {
        free(addresses_to_sort);
        addresses_to_sort = NULL;
        addresses_to_sort_capacity = 0;
        addresses_to_sort_count = 0;
      }
      
      flushOutputBuffer();
      freeHash(addrHash);
      addrHash = NULL; /* Reset for next file */
    }
    fclose(config->outFile_st);
    config->outFile_st = NULL;
  }

  return (EXIT_SUCCESS);
}

/****
 *
 * print addresses
 *
 ****/

int showAddresses(void) {
  FILE *output_stream;
  size_t i;
  
#ifdef DEBUG
  if (config->debug >= 1)
    printf("DEBUG - Finished processing file, printing\n");
#endif

  if (addrHash != NULL) {
    output_stream = config->outFile_st ? config->outFile_st : stdout;
    
    /* Reset collection arrays */
    addresses_to_sort_count = 0;
    
    /* Collect all addresses for sorting */
    traverseHash(addrHash, collectAddressForSorting);
    
    /* Sort addresses by frequency (desc) then IP (asc) */
    if (addresses_to_sort_count > 0) {
      qsort(addresses_to_sort, addresses_to_sort_count, sizeof(address_for_sorting_t), compare_addresses_for_output);
      
      /* Output addresses in sorted order */
      for (i = 0; i < addresses_to_sort_count; i++) {
        /* Print this address using the original printAddress logic */
        printAddress(addresses_to_sort[i].hash_record);
        
        /* Free the duplicated address string */
        free(addresses_to_sort[i].address);
      }
    }
    
    /* Clean up */
    if (addresses_to_sort != NULL) {
      free(addresses_to_sort);
      addresses_to_sort = NULL;
      addresses_to_sort_capacity = 0;
      addresses_to_sort_count = 0;
    }
    
    flushOutputBuffer();
    freeHash(addrHash);
    return (EXIT_SUCCESS);
  }

  return (EXIT_FAILURE);
}
