/*****
 *
 * Description: Log Pseudo Indexer Functions
 *
 * Copyright (c) 2008-2018, Ron Dilley
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

#include "searchpi.h"

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
 * search file
 *
 ****/

int searchFile(const char *fName) {
  FILE *inFile = NULL, *outFile = NULL;
  gzFile gzInFile;
  char inBuf[8192];
  char indexFileName[PATH_MAX];
  PRIVATE int c = 0, i;
  char *retPtr;
  struct hashRec_s *tmpRec;
  metaData_t *tmpMd;
  struct Address_s *tmpAddr;
  struct Fields_s **curFieldPtr;
  size_t offMatchPos = 0;
  size_t curMatchLine = 1;
  int isGz = FALSE;
  char *foundPtr;
  char indexBaseFileName[PATH_MAX];

  /* XXX need to look for index file first */
  if ((((foundPtr = strrchr(fName, '.')) != NULL)) &&
      (strncmp(foundPtr, ".gz", 3) EQ 0)) {
    isGz = TRUE;
    strncpy(indexBaseFileName, fName, foundPtr - fName);
    printf("DEBUG - IDXBase: %s\n", indexBaseFileName);
  }

  sprintf(indexFileName, "%s.lpi", fName);
  if ((loadIndexFile(indexFileName) EQ EXIT_FAILURE) || config->quick) {
    /* XXX if filename ends with '.gz', then look for different index name */
    if (isGz && !config->quick) {
      sprintf(indexFileName, "%s.lpi", indexBaseFileName);
      if (loadIndexFile(indexFileName) EQ EXIT_FAILURE)
        return (EXIT_FAILURE);
    } else
      return (EXIT_FAILURE);
  }
  /* XXX once index is loaded, search original file for hits */

  fprintf(stderr, "Opening [%s] for read\n", fName);

  if (isGz) {
    /* gzip compressed */
    if ((gzInFile = gzopen(fName, "rb")) EQ NULL) {
      fprintf(stderr, "ERR - Unable to open file [%s] %d (%s)\n", fName, errno,
              strerror(errno));
      return (EXIT_FAILURE);
    }
  } else {
#ifdef HAVE_FOPEN64
    if ((inFile = fopen64(fName, "r")) EQ NULL) {
#else
    if ((inFile = fopen(fName, "r")) EQ NULL) {
#endif
      fprintf(stderr, "ERR - Unable to open file [%s] %d (%s)\n", fName, errno,
              strerror(errno));
      return (EXIT_FAILURE);
    }
  }

  do {
    if (isGz)
      retPtr = gzgets(gzInFile, inBuf, sizeof(inBuf));
    else
      retPtr = fgets(inBuf, sizeof(inBuf), inFile);
    // printf("%zu %zu %zu\n", curMatchLine, offMatchPos,
    // config->match_offsets[offMatchPos]);

    if (curMatchLine EQ config->match_offsets[offMatchPos]) {
#ifdef DEBUG
      printf("[%zu] %s", curMatchLine, inBuf);
#else
      printf("%s", inBuf);
#endif

      while (config->match_offsets[offMatchPos] EQ curMatchLine)
        offMatchPos++;
    }

    curMatchLine++;
  } while ((retPtr != NULL) && (config->match_offsets[offMatchPos] > 0));

  if (isGz)
    gzclose(gzInFile);
  else
    fclose(inFile);

  return (EXIT_SUCCESS);
}

/****
 *
 * load index file
 *
 ****/

// 2c:c5:d3:4b:a7:bc,1,45624:10
// 58:97:bd:02:c2:ba,1,45600:10
// 58:97:bd:02:c2:bb,1,45599:10
// 2c:c5:d3:54:3d:9c,97,44841:10,44599:10,44395:10,44337:10,44094:10,43685:10,43440:10,43197:10,42565:10,42204:10,41964:10,41723:10,41437:10,41205:10,40871:10,40499:10,39280:10,38858:10,37631:10,37207:10,36960:10,35699:10,35437:10,35188:10,34658:10,34236:10,33985:10,32886:10,32527:10,32291:10,32057:10,31124:10,30888:10,30651:10,29718:10,29469:10,29220:10,28360:10,27989:10,27742:10,27497:10,26765:10,26342:10,26094:10,25849:10,25324:10,24897:10,24656:10,24415:10,23885:10,23467:10,23222:10,22980:10,21976:10,21726:10,21477:10,20466:10,20214:10,19973:10,19689:10,19027:10,18786:10,18545:10,18258:10,17531:10,17282:10,17036:10,16747:10,16514:10,16181:10,14706:10,14339:10,14091:10,13842:10,12883:10,12630:10,12383:10,10027:10,8274:10,7799:10,7552:10,7306:10,7062:10,6784:10,6129:10,5881:10,5633:10,5345:10,5114:10,4901:10,4484:10,4240:10,2988:10,2739:10,1772:10,1369:10,1127:10
// 2c:c5:d3:54:1c:3c,145,44898:10,44653:10,44159:10,43742:10,43500:10,43254:10,42970:10,42739:10,42259:10,42019:10,41778:10,41495:10,41257:10,41056:10,40564:10,40314:10,40065:10,39786:10,39349:10,38921:10,38674:10,38427:10,38143:10,37903:10,37698:10,37273:10,37025:10,36775:10,36495:10,36257:10,35766:10,35514:10,35247:10,34720:10,34302:10,34051:10,33800:10,33511:10,33270:10,33069:10,32583:10,32343:10,32109:10,31595:10,31183:10,30942:10,30706:10,30427:10,30196:10,29787:10,29536:10,29283:10,28996:10,28755:10,28550:10,28052:10,27805:10,27554:10,27270:10,26405:10,26157:10,25908:10,25622:10,25389:10,24964:10,24715:10,24472:10,23952:10,23530:10,23285:10,23039:10,22756:10,22522:10,22041:10,21791:10,21539:10,21250:10,21017:10,20529:10,20272:10,20030:10,19748:10,19511:10,19088:10,18845:10,18602:10,18321:10,18080:10,17596:10,17347:10,17096:10,16812:10,16367:10,15878:10,15637:10,15398:10,14404:10,14155:10,13903:10,13608:10,13370:10,12949:10,12696:10,12441:10,12157:10,11921:10,11723:10,11306:10,11061:10,10816:10,10533:10,10296:10,10094:10,9672:10,9428:10,9185:10,8904:10,8660:10,8458:10,7862:10,7614:10,7367:10,7118:10,6836:10,6605:10,6193:10,5944:10,5694:10,5407:10,5164:10,4964:10,4546:10,4302:10,4057:10,3546:10,3055:10,2803:10,2554:10,2265:10,2030:10,1835:10,1431:10,1184:10,939:10,31:10

/****
 *
 * load index file associated with input file
 *
 ****/

int loadIndexFile(const char *fName) {
  FILE *inFile = NULL;
  char inBuf[8192];
  char *tok;
  int i, done = FALSE;
  int match = FALSE;
  size_t a;
  size_t *offsets;
  size_t count;

  fprintf(stderr, "Opening [%s] for read\n", fName);
#ifdef HAVE_FOPEN64
  if ((inFile = fopen64(fName, "r")) EQ NULL) {
#else
  if ((inFile = fopen(fName, "r")) EQ NULL) {
#endif
    fprintf(stderr, "ERR - Unable to open file [%s] %d (%s)\n", fName, errno,
            strerror(errno));
    return (EXIT_FAILURE);
  }

  while ((fgets(inBuf, sizeof(inBuf), inFile) != NULL) && !done) {
    /* XXX test to see if the address matches a search term */
    /* XXX should impliment a high-speed search like boyer-moore */
    /* XXX this function does not handle multiple matches */
    tok = strtok(inBuf, ",");
    for (i = 0; config->search_terms[i] != NULL; i++) {
      // printf( "Searching for %s\n", config->search_terms[i]);
      if (strlen(config->search_terms[i]) EQ strlen(tok)) {
        if (XMEMCMP(config->search_terms[i], tok,
                    strlen(config->search_terms[i])) EQ 0) {
          match++;
          count = atol(strtok(NULL, ","));
          config->match_offsets =
              XREALLOC(config->match_offsets,
                       (config->match_count + 1 + count) * sizeof(size_t));
          fprintf(stderr, "MATCH [%s] with %zu lines\n", inBuf, count);
          for (a = config->match_count; a < (config->match_count + count);
               a++) {
            tok = strtok(NULL, ",");
            config->match_offsets[a] = atol(tok);
            // printf("%d %zu\n", a, config->match_offsets[a]);

            // printf("Offset: %zu\n", config->match_offsets[i]);
          }
          config->match_count += count;
        }
      }
    }
    if (match EQ i)
      done = TRUE;
  }

  /* sort the offset list */
  quickSort(config->match_offsets, 0, config->match_count - 1);

  // for (i = 0; i < config->match_count; i++)
  //  printf("Match: [%d] %zu\n", i, config->match_offsets[i]);

  fclose(inFile);

  if (match)
    return (EXIT_SUCCESS);
  else
    return (EXIT_FAILURE);
}

/****
 *
 * quick sort the offset array
 *
 ****/

void quickSort(size_t *number, size_t first, size_t last) {
  size_t i, j, pivot;
  size_t temp;

  if (first < last) {
    pivot = first;
    i = first;
    j = last;

    while (i < j) {
      while (number[i] <= number[pivot] && i < last)
        i++;
      while (number[j] > number[pivot])
        j--;
      if (i < j) {
        temp = number[i];
        number[i] = number[j];
        number[j] = temp;
      }
    }

    temp = number[pivot];
    number[pivot] = number[j];
    number[j] = temp;
    quickSort(number, first, j - 1);
    quickSort(number, j + 1, last);
  }
}

/****
 *
 * load search terms from file
 *
 ****/

int loadSearchFile(const char *fName) {
  FILE *inFile = NULL;
  char inBuf[8192];
  int i = 0;

  fprintf(stderr, "Opening [%s] for read\n", fName);
#ifdef HAVE_FOPEN64
  if ((inFile = fopen64(fName, "r")) EQ NULL) {
#else
  if ((inFile = fopen(fName, "r")) EQ NULL) {
#endif
    fprintf(stderr, "ERR - Unable to open file [%s] %d (%s)\n", fName, errno,
            strerror(errno));
    return (EXIT_FAILURE);
  }

  while (fgets(inBuf, sizeof(inBuf), inFile) != NULL) {
    config->search_terms =
        XREALLOC(config->search_terms, ((i + 2) * sizeof(char *)));
    config->search_terms[i] = XMALLOC(strlen(inBuf) + 1);
    XMEMSET(config->search_terms[i], 0, strlen(inBuf) + 1);
    config->search_terms[i + 1] = NULL;
    XMEMCPY(config->search_terms[i++], inBuf, strlen(inBuf) - 1);
    if (config->debug >= 3)
      printf("DEBUG - Before [%s]", inBuf);
  }

  fclose(inFile);

#ifdef DEBUG
  if (config->debug >= 2)
    printf("DEBUG - Loaded %d search terms from file\n", i);
#endif

  return (EXIT_SUCCESS);
}

/****
 *
 * process file
 *
 ****/

int processFile(const char *fName) {
  FILE *inFile = NULL, *outFile = NULL;
  char inBuf[8192];
  char outFileName[PATH_MAX];
  char patternBuf[4096];
  char oBuf[4096];
  PRIVATE int c = 0, i, ret;
  unsigned int totLineCount = 0, lineCount = 0, lineLen = 0,
               minLineLen = sizeof(inBuf), maxLineLen = 0, totLineLen = 0;
  unsigned int argCount = 0, totArgCount = 0, minArgCount = MAX_FIELD_POS,
               maxArgCount = 0;
  struct hashRec_s *tmpRec;
  metaData_t *tmpMd;
  struct Address_s *tmpAddr;
  struct Fields_s **curFieldPtr;

  /* initialize the hash if we need to */
  if (addrHash EQ NULL)
    addrHash = initHash(96);

  initParser();

  fprintf(stderr, "Opening [%s] for read\n", fName);
  if (strcmp(fName, "-") EQ 0) {
    inFile = stdin;
  } else {
#ifdef HAVE_FOPEN64
    if ((inFile = fopen64(fName, "r")) EQ NULL) {
#else
    if ((inFile = fopen(fName, "r")) EQ NULL) {
#endif
      fprintf(stderr, "ERR - Unable to open file [%s] %d (%s)\n", fName, errno,
              strerror(errno));
      return (EXIT_FAILURE);
    }
  }

  while (fgets(inBuf, sizeof(inBuf), inFile) != NULL && !quit) {
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

  if (inFile != stdin)
    fclose(inFile);

  deInitParser();

  return (EXIT_SUCCESS);
}
