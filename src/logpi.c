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
 * print all addr records in hash
 *
 ****/

int printAddress(const struct hashRec_s *hashRec) {
  metaData_t *tmpMd;
  struct Address_s *tmpAddr;

  if (hashRec->data != NULL) {
    tmpMd = (metaData_t *)hashRec->data;

#ifdef DEBUG
    if (config->debug >= 3)
      printf("DEBUG - Searching for [%s]\n", hashRec->keyString);
#endif

    /* save addr if -w was used */
    if (config->outFile_st != NULL)
      fprintf(config->outFile_st, "%s,%lu", hashRec->keyString + 1,
              tmpMd->count);
    else
      printf("%s,%lu", hashRec->keyString + 1, tmpMd->count);

    /* free the list of pseudo indexes */
    while ((tmpAddr = tmpMd->head) != NULL) {
      /* save addr if -w was used */
      if (config->outFile_st != NULL)
        fprintf(config->outFile_st, ",%lu:%lu", tmpAddr->line + 1,
                tmpAddr->offset);
      else
        printf(",%lu:%lu", tmpAddr->line + 1, tmpAddr->offset);

      tmpMd->head = tmpAddr->next;
      XFREE(tmpAddr);
    }
    if (config->outFile_st != NULL)
      fprintf(config->outFile_st, "\n");
    else
      printf("\n");
  }

  /* can use this later to interrupt traversing the hash */
  if (quit)
    return (TRUE);
  return (FALSE);
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

  /* initialize the hash if we need to */
  if (addrHash EQ NULL)
    addrHash = initHash(96);

  initParser();

  /* XXX need to add bzip2 */

  /* check to see if the file is compressed */
  if ((((foundPtr = strrchr(fName, '.')) != NULL)) &&
      (strncmp(foundPtr, ".gz", 3) EQ 0))
    isGz = TRUE;

  fprintf(stderr, "Opening [%s] for read\n", fName);

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
          if ((tmpRec = getHashRecord(addrHash, oBuf))
                  EQ NULL) { /* store line metadata */

            if ((tmpMd = (metaData_t *)XMALLOC(sizeof(metaData_t))) EQ NULL) {
              fprintf(stderr, "ERR - Unable to allocate memory, aborting\n");
              abort();
            }
            XMEMSET(tmpMd, 0, sizeof(metaData_t));
            tmpMd->count = 1;

            tmpAddr = (struct Address_s *)XMALLOC(sizeof(struct Address_s));
            XMEMSET(tmpAddr, 0, sizeof(struct Address_s));
            tmpMd->head = tmpAddr;
            tmpMd->head->line = totLineCount;
            tmpMd->head->offset = i;

            /* add to the hash */
            addUniqueHashRec(addrHash, oBuf, strlen(oBuf) + 1, tmpMd);

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
          } else {
            /* update the address counts */
            if (tmpRec->data != NULL) {
              tmpMd = (metaData_t *)tmpRec->data;
              tmpMd->count++;
              tmpAddr = (struct Address_s *)XMALLOC(sizeof(struct Address_s));
              XMEMSET(tmpAddr, 0, sizeof(struct Address_s));
              tmpAddr->next = tmpMd->head;
              tmpMd->head = tmpAddr;
              tmpMd->head->line = totLineCount;
              tmpMd->head->offset = i;
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

  return (EXIT_SUCCESS);
}

/****
 *
 * print addresses
 *
 ****/

int showAddresses(void) {
#ifdef DEBUG
  if (config->debug >= 1)
    printf("DEBUG - Finished processing file, printing\n");
#endif

  if (addrHash != NULL) {
    /* dump the template data */
    if (traverseHash(addrHash, printAddress) EQ TRUE) {
      freeHash(addrHash);
      return (EXIT_SUCCESS);
    }
    freeHash(addrHash);
  }

  return (EXIT_FAILURE);
}
