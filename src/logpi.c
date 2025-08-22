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

/* Global mega-buffer for batched output */
static char mega_buffer[1048576];  /* 1MB buffer */
static int mega_buffer_pos = 0;

int printAddress(const struct hashRec_s *hashRec) {
  metaData_t *tmpMd;
  struct Address_s *tmpAddr;
  char local_buffer[4096];
  int local_pos = 0;
  FILE *output_stream;

  if (hashRec->data != NULL) {
    tmpMd = (metaData_t *)hashRec->data;

#ifdef DEBUG
    if (config->debug >= 3)
      printf("DEBUG - Searching for [%s]\n", hashRec->keyString);
#endif

    /* Build output line in local buffer first */
    local_pos = snprintf(local_buffer, sizeof(local_buffer), "%s,%lu", 
                        hashRec->keyString + 1, tmpMd->count);

    /* Add all address:offset pairs to buffer */
    tmpAddr = tmpMd->head;
    while (tmpAddr != NULL && local_pos < sizeof(local_buffer) - 32) {
      local_pos += snprintf(local_buffer + local_pos, 
                          sizeof(local_buffer) - local_pos,
                          ",%lu:%lu", tmpAddr->line + 1, tmpAddr->offset);
      tmpAddr = tmpAddr->next;
    }
    
    /* Add newline */
    if (local_pos < sizeof(local_buffer) - 1) {
      local_buffer[local_pos++] = '\n';
    }

    /* Add to mega buffer */
    if (mega_buffer_pos + local_pos >= sizeof(mega_buffer)) {
      /* Flush mega buffer when full */
      output_stream = config->outFile_st ? config->outFile_st : stdout;
      fwrite(mega_buffer, 1, mega_buffer_pos, output_stream);
      mega_buffer_pos = 0;
    }
    
    /* Copy to mega buffer */
    memcpy(mega_buffer + mega_buffer_pos, local_buffer, local_pos);
    mega_buffer_pos += local_pos;

    /* Free the list of pseudo indexes */
    while ((tmpAddr = tmpMd->head) != NULL) {
      tmpMd->head = tmpAddr->next;
      XFREE(tmpAddr);
    }
  }

  /* can use this later to interrupt traversing the hash */
  if (quit)
    return (TRUE);
  return (FALSE);
}

/* Flush any remaining buffered output */
void flushOutputBuffer(void) {
  if (mega_buffer_pos > 0) {
    FILE *output_stream = config->outFile_st ? config->outFile_st : stdout;
    fwrite(mega_buffer, 1, mega_buffer_pos, output_stream);
    mega_buffer_pos = 0;
  }
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

  /* For auto-naming, write addresses to file and close it */
  if (config->auto_lpi_naming && config->outFile_st) {
    /* Write addresses to this file */
    if (addrHash != NULL) {
      traverseHash(addrHash, printAddress);
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
#ifdef DEBUG
  if (config->debug >= 1)
    printf("DEBUG - Finished processing file, printing\n");
#endif

  if (addrHash != NULL) {
    /* dump the template data */
    if (traverseHash(addrHash, printAddress) EQ TRUE) {
      flushOutputBuffer();
      freeHash(addrHash);
      return (EXIT_SUCCESS);
    }
    flushOutputBuffer();
    freeHash(addrHash);
  }

  return (EXIT_FAILURE);
}
