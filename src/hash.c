/*****
 *
 * Description: Hash Functions
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
 * defines
 *
 ****/

/****
 *
 * includes
 *
 ****/

#include "hash.h"

/****
 *
 * local variables
 *
 ****/

/* Prime numbers for hash table sizing */
static size_t hashPrimes[] = {
    53, 97, 193, 389, 769, 1543, 3079,
    6151, 12289, 24593, 49157, 98317, 196613, 393241,
    786433, 1572869, 3145739, 6291469, 12582917, 25165843, 50331653,
    100663319, 201326611, 402653189, 805306457, 1610612741, 0
};

/* FNV-1a hash constants */
#define FNV_OFFSET_BASIS 2166136261U
#define FNV_PRIME 16777619U

/* Memory pool constants */
#define POOL_SIZE 1024

/****
 *
 * external global variables
 *
 ****/

extern Config_t *config;

/****
 *
 * Memory pool management
 *
 ****/

static struct hashRec_s *allocHashRecord(struct hash_s *hash)
{
  struct hashRecPool_s *pool = hash->pools;
  struct hashRec_s *record;
  
  /* Find pool with available space */
  while (pool && pool->used >= pool->capacity) {
    pool = pool->next;
  }
  
  /* Create new pool if needed */
  if (!pool) {
    if ((pool = (struct hashRecPool_s *)XMALLOC(sizeof(struct hashRecPool_s))) == NULL)
      return NULL;
      
    if ((pool->records = (struct hashRec_s *)XMALLOC(sizeof(struct hashRec_s) * POOL_SIZE)) == NULL) {
      XFREE(pool);
      return NULL;
    }
    
    pool->capacity = POOL_SIZE;
    pool->used = 0;
    pool->next = hash->pools;
    hash->pools = pool;
  }
  
  /* Return next available record */
  record = &pool->records[pool->used++];
  XMEMSET(record, 0, sizeof(struct hashRec_s));
  return record;
}

static void freePools(struct hash_s *hash)
{
  struct hashRecPool_s *pool = hash->pools;
  struct hashRecPool_s *next;
  
  while (pool) {
    next = pool->next;
    if (pool->records)
      XFREE(pool->records);
    XFREE(pool);
    pool = next;
  }
  hash->pools = NULL;
}

/****
 *
 * FNV-1a hash function
 *
 ****/

uint32_t fnv1aHash(const char *keyString, int keyLen)
{
  uint32_t hash = FNV_OFFSET_BASIS;
  int i;
  
  for (i = 0; i < keyLen; i++) {
    hash ^= (uint8_t)keyString[i];
    hash *= FNV_PRIME;
  }
  
  return hash;
}

/****
 *
 * Calculate hash value with length (optimized wrapper)
 *
 ****/

uint32_t calcHashWithLen(const char *keyString, int keyLen)
{
  return fnv1aHash(keyString, keyLen);
}

/****
 *
 * functions
 *
 ****/

/****
 *
 * calculate hash
 *
 ****/

uint32_t calcHash(uint32_t hashSize, const char *keyString) {
  int32_t val = 0;
  const char *ptr;
  int i, tmp, keyLen = strlen( keyString ) + 1;
  
#ifdef DEBUG
  if (config->debug >= 3)
    printf("DEBUG - Calculating hash\n");
#endif

  /* generate the lookup hash */
  for (i = 0; i < keyLen; i++) {
    val = (val << 4) + (keyString[i] & 0xff);
    if ((tmp = (val & 0xf0000000))) {
      val = val ^ (tmp >> 24);
      val = val ^ tmp;
    }
  }

#ifdef DEBUG
  if (config->debug >= 4)
    printf("DEBUG - hash: %d\n", val % hashSize);
#endif

  return val % hashSize;
}

/****
 *
 * empty the hash table
 *
 ****/

void freeHash(struct hash_s *hash) {
  uint32_t key;
  struct hashRec_s *record, *next;
  
  if (hash == NULL)
    return;
    
  /* Free bucket chains and key strings */
  if (hash->buckets != NULL) {
    for (key = 0; key < hash->size; key++) {
      record = hash->buckets[key];
      while (record) {
        next = record->next;
        if (record->keyString)
          XFREE(record->keyString);
        record = next;
      }
    }
    XFREE(hash->buckets);
  }
  
  /* Free memory pools */
  freePools(hash);
  
  XFREE(hash);
}

/****
 *
 * traverse all hash records, calling func() for each one
 *
 ****/

int traverseHash(const struct hash_s *hash,
                 int (*fn)(const struct hashRec_s *hashRec)) {
  uint32_t bucket;
  struct hashRec_s *record;
  
  if (!hash || !fn)
    return FAILED;
    
#ifdef DEBUG
  if (config->debug >= 3)
    printf("DEBUG - Traversing hash table\n");
#endif
  
  /* Traverse all buckets and their chains */
  for (bucket = 0; bucket < hash->size; bucket++) {
    record = hash->buckets[bucket];
    while (record) {
      if (fn(record))
        return FAILED;
      record = record->next;
    }
  }
  
  return TRUE;
}

/****
 *
 * add a record to the hash
 *
 ****/

int addHashRec(struct hash_s *hash, uint32_t key, char *keyString, void *data,
               time_t lastSeen) {
  struct hashRec_s *tmpHashRec;
  struct hashRec_s *curHashRec;
  int tmpDepth = 0;

#ifdef DEBUG
  if (config->debug >= 3)
    printf("DEBUG - Adding hash [%d] (%s)\n", key, keyString);
#endif

  if (hash->buckets[key] EQ NULL) {
    /* nope, add it in the current slot */
    if ((hash->buckets[key] =
             (struct hashRec_s *)XMALLOC(sizeof(struct hashRec_s))) EQ NULL) {
      fprintf(stderr, "ERR - Unable to allocate space for hash\n");
      return FAILED;
    }
    XMEMSET((struct hashRec_s *)hash->buckets[key], 0,
            sizeof(struct hashRec_s));
    if ((hash->buckets[key]->keyString = (char *)XMALLOC(strlen(keyString) + 1))
            EQ NULL) {
      fprintf(stderr, "ERR - Unable to allocate space for hash label\n");
      XFREE(hash->buckets[key]);
      return FAILED;
    }
    XSTRCPY(hash->buckets[key]->keyString, keyString);
    hash->buckets[key]->data = data;
    hash->buckets[key]->lastSeen = hash->buckets[key]->createTime = lastSeen;
    tmpDepth++;
  } else {
    /* yup, traverse the linked list and stick it at the end */

    /* XXX we should make this dynamically optimizing, so the most accessed is
     * at the top of the list */

    /* advance to the end of the chain */
    curHashRec = hash->buckets[key];
    while (curHashRec != NULL) {
      if (curHashRec->next != NULL) {
        curHashRec = curHashRec->next;
        tmpDepth++;
      } else {
        /* at the end of the chain */
        if ((curHashRec->next = (struct hashRec_s *)XMALLOC(
                 sizeof(struct hashRec_s))) EQ NULL) {
          fprintf(stderr, "ERR - Unable to allocate space for hash\n");
          return FAILED;
        }
        XMEMSET((struct hashRec_s *)curHashRec->next, 0,
                sizeof(struct hashRec_s));
        if ((curHashRec->next->keyString =
                 (char *)XMALLOC(strlen(keyString) + 1)) EQ NULL) {
          fprintf(stderr, "ERR - Unable to allocate space for hash label\n");
          XFREE(curHashRec->next);
          curHashRec->next = NULL;
          return FAILED;
        }
        XSTRCPY(curHashRec->next->keyString, keyString);
        curHashRec->next->data = data;
        curHashRec->next->lastSeen = curHashRec->next->createTime = lastSeen;
        curHashRec = NULL;
      }
    }
  }

  if (hash->maxDepth < tmpDepth)
    hash->maxDepth = tmpDepth;

  hash->totalRecords++;

  return TRUE;
}

/****
 *
 * add a record to the hash
 *
 ****/

int addUniqueHashRec(struct hash_s *hash, const char *keyString, int keyLen,
                     void *data) {
  uint32_t hashValue;
  uint32_t bucket;
  struct hashRec_s *record, *newRecord;
  uint16_t depth = 0;
  
  if (!hash || !keyString)
    return FAILED;
    
  if (keyLen == 0)
    keyLen = strlen(keyString) + 1;
    
  /* Calculate hash and bucket */
  hashValue = fnv1aHash(keyString, keyLen);
  bucket = hashValue % hash->size;
  
  /* Check for existing record */
  record = hash->buckets[bucket];
  while (record) {
    if (record->hashValue == hashValue &&
        record->keyLen == keyLen &&
        XMEMCMP(record->keyString, keyString, keyLen) == 0) {
      /* Found existing record - update access time */
      record->lastSeen = config->current_time;
      record->accessCount++;
      return FAILED; /* Duplicate */
    }
    record = record->next;
    depth++;
  }
  
  /* Allocate new record from pool */
  if ((newRecord = allocHashRecord(hash)) == NULL) {
    fprintf(stderr, "ERR - Unable to allocate hash record\n");
    return FAILED;
  }
  
  /* Allocate and copy key string */
  if ((newRecord->keyString = (char *)XMALLOC(keyLen)) == NULL) {
    fprintf(stderr, "ERR - Unable to allocate key string\n");
    return FAILED;
  }
  XMEMCPY(newRecord->keyString, (void *)keyString, keyLen);
  
  /* Initialize record */
  newRecord->keyLen = keyLen;
  newRecord->hashValue = hashValue;
  newRecord->data = data;
  newRecord->lastSeen = newRecord->createTime = config->current_time;
  newRecord->accessCount = 1;
  newRecord->modifyCount = 0;
  
  /* Add to front of bucket chain */
  newRecord->next = hash->buckets[bucket];
  hash->buckets[bucket] = newRecord;
  
  /* Update statistics */
  hash->totalRecords++;
  if (depth > hash->maxDepth)
    hash->maxDepth = depth;
    
#ifdef DEBUG
  if (config->debug >= 4)
    printf("DEBUG - Added hash record [bucket:%u, depth:%u, total:%u]\n", 
           bucket, depth, hash->totalRecords);
#endif
  
  return TRUE;
}

/****
 *
 * initialize the hash
 *
 ****/

struct hash_s *initHash(uint32_t hashSize) {
  struct hash_s *tmpHash;
  int i;
  
  if ((tmpHash = (struct hash_s *)XMALLOC(sizeof(struct hash_s))) == NULL) {
    fprintf(stderr, "ERR - Unable to allocate hash\n");
    return NULL;
  }
  XMEMSET(tmpHash, 0, sizeof(struct hash_s));
  
  /* Pick a good prime hash size */
  for (i = 0; ((hashSize > hashPrimes[i]) && (hashPrimes[i] > 0)); i++)
    ;
  
  if (hashPrimes[i] == 0) {
    fprintf(stderr, "ERR - Hash size too large\n");
    XFREE(tmpHash);
    return NULL;
  }
  
  tmpHash->primeOff = i;
  tmpHash->size = hashPrimes[i];
  
  /* Allocate bucket array */
  if ((tmpHash->buckets = (struct hashRec_s **)XMALLOC(
           sizeof(struct hashRec_s *) * tmpHash->size)) == NULL) {
    fprintf(stderr, "ERR - Unable to allocate hash buckets\n");
    XFREE(tmpHash);
    return NULL;
  }
  XMEMSET(tmpHash->buckets, 0, sizeof(struct hashRec_s *) * tmpHash->size);
  
  tmpHash->pools = NULL;
  tmpHash->totalRecords = 0;
  tmpHash->maxDepth = 0;
  
#ifdef DEBUG
  if (config->debug >= 4)
    printf("DEBUG - Hash initialized [%u]\n", tmpHash->size);
#endif
  
  return tmpHash;
}

/****
 *
 * find a hash
 *
 ****/

uint32_t searchHash(struct hash_s *hash, const char *keyString) {
  struct hashRec_s *tmpHashRec = NULL;
//  uint32_t key = calcHash(hash->size, keyString);
  uint32_t key;
  int depth = 0;
  int keyLen = strlen(keyString)+1;
  int i, tmp;
  int32_t val = 0;
  
  /* generate the lookup hash */
  for (i = 0; i < keyLen; i++) {
    val = (val << 4) + (keyString[i] & 0xff);
    if ((tmp = (val & 0xf0000000))) {
      val = val ^ (tmp >> 24);
      val = val ^ tmp;
    }
  }
  key = val % hash->size;
  
#ifdef DEBUG
  if (config->debug >= 3)
    printf("DEBUG - Searching for (%s) in hash table at [%d]\n",
           (char *)keyString, key);
#endif

  /* check to see if the hash slot is allocated */
  if (hash->buckets[key] EQ NULL) {
    /* empty hash slot */
#ifdef DEBUG
    if (config->debug >= 5)
      printf("DEBUG - (%s) not found in hash table\n", (char *)keyString);
#endif
    return hash->size + 1;
  }

  /* XXX switch to a single while loop */
  tmpHashRec = hash->buckets[key];
  while (tmpHashRec != NULL) {
    if (tmpHashRec->keyLen EQ keyLen) {
      if (strcmp((char *)tmpHashRec->keyString, (char *)keyString) EQ 0) {
#ifdef DEBUG
        if (config->debug >= 5)
          printf("DEBUG - Found (%s) in hash table at [%d] at depth [%d]\n",
                 (char *)keyString, key, depth);
#endif
        tmpHashRec->lastSeen = time(NULL);
        tmpHashRec->accessCount++;
        return key;
      }
    }
    tmpHashRec = tmpHashRec->next;
  }

#ifdef DEBUG
  if (config->debug >= 4)
    printf("DEBUG - (%s) not found in hash table\n", (char *)keyString);
#endif

  return hash->size + 1;
}

/****
 *
 * get hash record pointer
 *
 ****/

struct hashRec_s *getHashRecord(struct hash_s *hash, const void *keyString) {
  uint32_t hashValue;
  uint32_t bucket;
  struct hashRec_s *record;
  int keyLen = strlen(keyString) + 1;
  
  if (!hash || !keyString)
    return NULL;
    
  /* Calculate hash and bucket */
  hashValue = fnv1aHash(keyString, keyLen);
  bucket = hashValue % hash->size;
  
  /* Search bucket chain */
  record = hash->buckets[bucket];
  while (record) {
    if (record->hashValue == hashValue &&
        record->keyLen == keyLen &&
        XMEMCMP(record->keyString, keyString, keyLen) == 0) {
      /* Found record - update access info */
      record->lastSeen = config->current_time;
      record->accessCount++;
      return record;
    }
    record = record->next;
  }
  
  return NULL; /* Not found */
}

/****
 *
 * snoop hash record pointer with precomputed key
 *
 ****/

inline struct hashRec_s *snoopHashRecWithKey(struct hash_s *hash,
                                             const char *keyString, int keyLen,
                                             uint32_t key) {
  struct hashRec_s *tmpHashRec;
  const char *ptr;
  int depth = 0;
#ifdef DEBUG
  char oBuf[4096];
  char nBuf[4096];
#endif

#ifdef DEBUG
  if (config->debug >= 3)
    printf("DEBUG - Searching for [%s]\n",
           hexConvert(keyString, keyLen, nBuf, sizeof(nBuf)));
#endif

  // if ( keyLen EQ 0 )
  //  keyLen = strlen( keyString );

  /* XXX switch to a single while loop */

  tmpHashRec = hash->buckets[key];
  while (tmpHashRec != NULL) {
    if (bcmp(tmpHashRec->keyString, keyString, keyLen) EQ 0) {
#ifdef DEBUG
      if (config->debug >= 4)
        printf("DEBUG - Found (%s) in hash table at [%d] at depth [%d] [%s]\n",
               hexConvert(keyString, keyLen, nBuf, sizeof(nBuf)), key, depth,
               hexConvert(tmpHashRec->keyString, tmpHashRec->keyLen, oBuf,
                          sizeof(oBuf)));
#endif
      return tmpHashRec;
    }
    tmpHashRec = tmpHashRec->next;
    depth++;
  }

  return NULL;
}

/****
 *
 * snoop hash record pointer
 *
 ****/

struct hashRec_s *snoopHashRecord(struct hash_s *hash, const char *keyString,
                                  int keyLen) {
  uint32_t key;
  int depth = 0;
  struct hashRec_s *tmpHashRec;
  uint32_t val = 0;
  const char *ptr;
  char oBuf[4096];
  char nBuf[4096];
  int i = 0;

#ifdef DEBUG
  if (config->debug >= 3)
    printf("DEBUG - Searching for [%s]\n",
           hexConvert(keyString, keyLen, nBuf, sizeof(nBuf)));
#endif

  if (keyLen EQ 0)
    keyLen = strlen(keyString);

  /* generate the lookup hash */
  for (i = 0; i < keyLen; i++) {
    int tmp;
    val = (val << 4) + (keyString[i] & 0xff);
    if ((tmp = (val & 0xf0000000))) {
      val = val ^ (tmp >> 24);
      val = val ^ tmp;
    }
  }
  key = val % hash->size;

  /* XXX switch to a single while loop */

  tmpHashRec = hash->buckets[key];
  while (tmpHashRec != NULL) {
    if (bcmp(tmpHashRec->keyString, keyString, keyLen) EQ 0) {
#ifdef DEBUG
      if (config->debug >= 4)
        printf("DEBUG - Found (%s) in hash table at [%d] at depth [%d] [%s]\n",
               hexConvert(keyString, keyLen, nBuf, sizeof(nBuf)), key, depth,
               hexConvert(tmpHashRec->keyString, tmpHashRec->keyLen, oBuf,
                          sizeof(oBuf)));
#endif
      return tmpHashRec;
    }
    depth++;
    tmpHashRec = tmpHashRec->next;
  }

  return NULL;
}

/****
 *
 * get data in hash record
 *
 ****/

void *getHashData(struct hash_s *hash, const void *keyString) {
  uint32_t key = calcHash(hash->size, keyString);
  struct hashRec_s *tmpHashRec;

  return getDataByKey(hash, key, (void *)keyString);
}

void *getDataByKey(struct hash_s *hash, uint32_t key, void *keyString) {
  int depth = 0;
  struct hashRec_s *tmpHashRec;

#ifdef DEBUG
  if (config->debug >= 3)
    printf("DEBUG - Getting data from hash table\n");
#endif

  tmpHashRec = hash->buckets[key];
  while (tmpHashRec != NULL) {
    if (strcmp(tmpHashRec->keyString, (char *)keyString) EQ 0) {
#ifdef DEBUG
      if (config->debug >= 4)
        printf("DEBUG - Found (%s) in hash table at [%d] at depth [%d]\n",
               (char *)keyString, key, depth);
#endif
      /* XXX would be faster with a global current time updated periodically */
      tmpHashRec->lastSeen = time(NULL);
      tmpHashRec->accessCount++;
      return tmpHashRec->data;
    }
    depth++;
    tmpHashRec = tmpHashRec->next;
  }

  return NULL;
}

/****
 *
 * dump the hash
 *
 ****/

void dumpHash(struct hash_s *hash) {
  uint32_t key = 0;
  int count = 0;
  struct hashRec_s *tmpHashRec;

  for (key = 0; key < hash->size; key++) {
    tmpHashRec = hash->buckets[key];
    while (tmpHashRec != NULL) {
      if (tmpHashRec->keyString != NULL) {
        // fprintf( stderr, "%d: %s\n", key, tmpHashRec->keyString );
        count++;
      }
      tmpHashRec = tmpHashRec->next;
    }
  }

  // fprintf( stderr, "%d total items\n", count );
}

/****
 *
 * grow the hash
 *
 ****/

struct hash_s *growHash(struct hash_s *oldHash, size_t newHashSize) {
  return NULL;
}

/****
 *
 * shrink the hash
 *
 ****/

struct hash_s *shrinkHash(struct hash_s *oldHash, size_t newHashSize) {
  return NULL;
}

/****
 *
 * dynamic hash grow
 *
 ****/

struct hash_s *dyGrowHash(struct hash_s *oldHash) {
  struct hash_s *newHash;
  uint32_t bucket;
  struct hashRec_s *record, *next, *newRecord;
  
  if (!oldHash || oldHash->primeOff >= (sizeof(hashPrimes)/sizeof(hashPrimes[0]) - 2))
    return oldHash;
    
  /* Create new larger hash */
  if ((newHash = initHash(hashPrimes[oldHash->primeOff + 1])) == NULL) {
    fprintf(stderr, "ERR - Unable to allocate new hash\n");
    return oldHash;
  }
  
  /* Rehash all records */
  for (bucket = 0; bucket < oldHash->size; bucket++) {
    record = oldHash->buckets[bucket];
    while (record) {
      next = record->next;
      
      /* Recalculate bucket in new table */
      uint32_t newBucket = record->hashValue % newHash->size;
      
      /* Allocate new record */
      if ((newRecord = allocHashRecord(newHash)) == NULL) {
        fprintf(stderr, "ERR - Failed to allocate during grow\n");
        freeHash(newHash);
        return oldHash;
      }
      
      /* Copy record (shallow copy of data pointer) */
      newRecord->keyLen = record->keyLen;
      newRecord->hashValue = record->hashValue;
      newRecord->data = record->data;
      newRecord->lastSeen = record->lastSeen;
      newRecord->createTime = record->createTime;
      newRecord->accessCount = record->accessCount;
      newRecord->modifyCount = record->modifyCount;
      
      /* Allocate and copy key */
      if ((newRecord->keyString = (char *)XMALLOC(record->keyLen)) == NULL) {
        fprintf(stderr, "ERR - Failed to allocate key during grow\n");
        freeHash(newHash);
        return oldHash;
      }
      XMEMCPY(newRecord->keyString, record->keyString, record->keyLen);
      
      /* Add to new hash bucket */
      newRecord->next = newHash->buckets[newBucket];
      newHash->buckets[newBucket] = newRecord;
      newHash->totalRecords++;
      
      record = next;
    }
  }
  
#ifdef DEBUG
  if (config->debug >= 2)
    printf("DEBUG - Grew hash from %u to %u buckets\n", oldHash->size, newHash->size);
#endif
  
  /* Free old hash */
  freeHash(oldHash);
  
  return newHash;
}

/****
 *
 * dynamic hash shring
 *
 ****/

struct hash_s *dyShrinkHash(struct hash_s *oldHash) {
  struct hash_s *tmpHash;
  int i;
  uint32_t tmpKey;

  if ((oldHash->totalRecords / oldHash->size) < 0.3) {
    /* the hash should be shrunk */
    if (oldHash->primeOff EQ 0)
      return oldHash;

    if ((tmpHash = initHash(hashPrimes[oldHash->primeOff - 1])) EQ NULL) {
      fprintf(stderr, "ERR - Unable to allocate new hash\n");
      return oldHash;
    }

    for (i = 0; i < oldHash->size; i++) {
      if (oldHash->buckets[i] != NULL) {
        /* move hash records */
        tmpKey = calcHash(tmpHash->size, oldHash->buckets[i]->keyString);
        tmpHash->buckets[tmpKey] = oldHash->buckets[i];
        oldHash->buckets[i] = NULL;
      }
    }

    tmpHash->totalRecords = oldHash->totalRecords;
    tmpHash->maxDepth = oldHash->maxDepth;
    freeHash(oldHash);
    return tmpHash;
  }

  return oldHash;
}

/****
 *
 * remove hash record
 *
 ****/

void *deleteHashRecord(struct hash_s *hash, const char *keyString, int keyLen) {
  uint32_t hashValue;
  uint32_t bucket;
  struct hashRec_s *record, *prevRecord = NULL;
  void *data;

  if (!hash || !keyString)
    return NULL;
    
  if (keyLen == 0)
    keyLen = strlen(keyString);

  /* Calculate hash and bucket */
  hashValue = fnv1aHash(keyString, keyLen);
  bucket = hashValue % hash->size;

  /* Search bucket chain */
  record = hash->buckets[bucket];
  while (record) {
    if (record->hashValue == hashValue &&
        record->keyLen == keyLen &&
        XMEMCMP(record->keyString, keyString, keyLen) == 0) {
      
#ifdef DEBUG
      if (config->debug >= 3)
        printf("DEBUG - Removing hash record\n");
#endif
      
      /* Remove from chain */
      if (prevRecord)
        prevRecord->next = record->next;
      else
        hash->buckets[bucket] = record->next;

      /* Save data and free record */
      data = record->data;
      XFREE(record->keyString);
      XFREE(record);
      hash->totalRecords--;

      return data;
    }
    prevRecord = record;
    record = record->next;
  }

  return NULL;
}

/****
 *
 * get an old record
 *
 ****/

/* XXX really inefficient, should generate a list of old records and return a
 * linked list */

void *purgeOldHashData(struct hash_s *hash, time_t age) {
  uint32_t bucket;
  struct hashRec_s *record, *prevRecord, *next;
  void *data;

#ifdef DEBUG
  if (config->debug >= 3)
    printf("DEBUG - Purging hash records older than [%u]\n", (unsigned int)age);
#endif

  for (bucket = 0; bucket < hash->size; bucket++) {
    prevRecord = NULL;
    record = hash->buckets[bucket];
    
    while (record != NULL) {
      next = record->next;
      
      if (record->lastSeen == 0) {
        fprintf(stderr, "ERR - hash rec with bad time\n");
        prevRecord = record;
      } else if (record->lastSeen < age) {
#ifdef DEBUG
        if (config->debug >= 4)
          printf("DEBUG - Removing old hash record\n");
#endif
        /* Remove from chain */
        if (prevRecord)
          prevRecord->next = record->next;
        else
          hash->buckets[bucket] = record->next;
          
        hash->totalRecords--;
        
        /* Save data and free record */
        data = record->data;
        XFREE(record->keyString);
        XFREE(record);
        
        /* Return first old data found */
        if (data != NULL)
          return data;
      } else {
        prevRecord = record;
      }
      record = next;
    }
  }

  /* no old records */
  return NULL;
}

/****
 *
 * pop data out of hash
 *
 ****/

/* XXX inefficient, should generate a list of records to pop and return a linked
 * list */

void *popHash(struct hash_s *hash) {
  uint32_t bucket;
  struct hashRec_s *record;
  void *data;

#ifdef DEBUG
  printf("DEBUG - POPing hash record\n");
#endif

  for (bucket = 0; bucket < hash->size; bucket++) {
    record = hash->buckets[bucket];
    if (record != NULL) {
#ifdef DEBUG
      printf("DEBUG - Popping hash record\n");
#endif
      /* Remove first record from bucket */
      hash->buckets[bucket] = record->next;
      hash->totalRecords--;
      
      /* Save data and free record */
      data = record->data;
      XFREE(record->keyString);
      XFREE(record);
      
      /* Return first data found */
      if (data != NULL)
        return data;
    }
  }

  /* no records */
  return NULL;
}

/****
 *
 * print key string in hex (just in case it is not ascii)
 *
 ****/

char *hexConvert(const char *keyString, int keyLen, char *buf,
                 const int bufLen) {
  int i;
  char *ptr = buf;
  for (i = 0; i < keyLen & i < (bufLen / 2) - 1; i++) {
    snprintf(ptr, bufLen, "%02x", keyString[i] & 0xff);
    ptr += 2;
  }
  return buf;
}

/****
 *
 * print key string in hex (just in case it is not ascii)
 *
 ****/

char *utfConvert(const char *keyString, int keyLen, char *buf,
                 const int bufLen) {
  int i;
  char *ptr = buf;
  /* XXX should check for buf len */
  for (i = 0; i < (keyLen / 2); i++) {
    buf[i] = keyString[(i * 2)];
  }
  buf[i] = '\0';

  return buf;
}

/****
 *
 * return size of hash
 *
 ****/

uint32_t getHashSize(struct hash_s *hash) {
  if (hash != NULL)
    return hash->size;
  return FAILED;
}
