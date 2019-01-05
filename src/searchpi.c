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

int searchFile(const char *fName)
{
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
  int done = FALSE, isGz = FALSE;
  char *foundPtr;
  char indexBaseFileName[PATH_MAX];

  if ( config->out_filename != NULL ) {
    if ( ( outFile = fopen( config->out_filename, "w" ) ) EQ NULL ) {
      fprintf( stderr, "ERR - Unable to open file [%s]\n", config->out_filename );
      return( EXIT_FAILURE );
    }
  } else
    outFile = stdout;

  if ((((foundPtr = strrchr(fName, '.')) != NULL)) &&
      (strncmp(foundPtr, ".gz", 3) EQ 0))
  {
    isGz = TRUE;
    strncpy(indexBaseFileName, fName, foundPtr - fName);
    indexBaseFileName[foundPtr - fName] = '\0';
  }

  sprintf(indexFileName, "%s.lpi", fName);
  if ((loadIndexFile(indexFileName) EQ EXIT_FAILURE) || config->quick)
  {
    if (isGz && !config->quick)
    {
      sprintf(indexFileName, "%s.lpi", indexBaseFileName);
      if (loadIndexFile(indexFileName) EQ EXIT_FAILURE)
        return (EXIT_FAILURE);
    }
    else
      return (EXIT_FAILURE);
  }

  /* XXX need to add support for bzip2 */
  
  fprintf(stderr, "Opening [%s] for read\n", fName);
  /* XXX switch to multiple compression types */
  if (isGz)
  {
    /* gzip compressed */
    if ((gzInFile = gzopen(fName, "rb")) EQ NULL)
    {
      fprintf(stderr, "ERR - Unable to open file [%s] %d (%s)\n", fName, errno,
              strerror(errno));
      return (EXIT_FAILURE);
    }
  }
  else
  {
#ifdef HAVE_FOPEN64
    if ((inFile = fopen64(fName, "r")) EQ NULL)
    {
#else
    if ((inFile = fopen(fName, "r")) EQ NULL)
    {
#endif
      fprintf(stderr, "ERR - Unable to open file [%s] %d (%s)\n", fName, errno,
              strerror(errno));
      return (EXIT_FAILURE);
    }
  }

  do
  {
    /* XXX switch to multiple compression types */
    if (isGz)
      retPtr = gzgets(gzInFile, inBuf, sizeof(inBuf));
    else
      retPtr = fgets(inBuf, sizeof(inBuf), inFile);

#ifdef DEBUG
    if ( config->debug >= 4 )
      fprintf( stderr, "DEBUG - CURLINE: %zu OFFPOS: %zu NEXTMATCH: %zu MATCHCOUNT: %zu\n", curMatchLine, offMatchPos, config->match_offsets[offMatchPos], config->match_count );
#endif

    if (curMatchLine EQ config->match_offsets[offMatchPos])
    {
#ifdef DEBUG
      fprintf(outFile, "[%zu] %s", curMatchLine, inBuf);
#else
      fprintf( outFile, "%s", inBuf);
#endif
      while ( config->match_offsets[offMatchPos] EQ curMatchLine )
        offMatchPos++;
         
      if (offMatchPos >= config->match_count)
        done = TRUE;
    }

    curMatchLine++;
  } while ((retPtr != NULL) && !done);

  /* XXX switch to multiple compression types */
  if (isGz)
    gzclose(gzInFile);
  else
    fclose(inFile);

  if ( config->out_filename != NULL )
    fclose( outFile );
    
  /* cleanup global variables so we can process more files */
  XFREE( config->match_offsets );
  config->match_offsets = NULL;
  config->match_count = 0;

  return (EXIT_SUCCESS);
}

/****
 *
 * load index file associated with input file
 *
 ****/

int loadIndexFile(const char *fName)
{
  FILE *inFile = NULL;
  char inBuf[65536], *tok, *sol, *endPtr, *eol, *lineBuf = NULL;
  int i, done = FALSE, match = 0;
  size_t a, count, linePos = 0, *offsets, rCount, rLeft, lineBufSize = 0;
  struct searchTerm_s *searchHead = NULL, *searchTail = NULL, *searchPtr, *tmpPtr;

  /* make a copy of the term linked list */
  searchPtr = config->searchHead;
  while( searchPtr != NULL ) {
    /* create new search term record */
    if ((tmpPtr = XMALLOC(sizeof(struct searchTerm_s))) EQ NULL)
    {
      fprintf(stderr, "ERR - Unable to allocate memory for search term\n");
      exit(EXIT_FAILURE);
    }
    XMEMSET(tmpPtr, '\0', sizeof(struct searchTerm_s));
    tmpPtr->len = searchPtr->len;
    tmpPtr->term = XMALLOC(searchPtr->len + 1);
    XSTRCPY(tmpPtr->term, searchPtr->term );

    /* store search term in the linked list */
    tmpPtr->next = searchHead;
    if ( searchHead != NULL )
      searchHead->prev = tmpPtr;
    searchHead = tmpPtr;

    /* advance to next source record */
    searchPtr = searchPtr->next;
  }

#ifdef DEBUG
  if (config->debug >= 1)
    fprintf(stderr, "Opening [%s] for read\n", fName);
#endif

#ifdef HAVE_FOPEN64
  if ((inFile = fopen64(fName, "r")) EQ NULL)
  {
#else
  if ((inFile = fopen(fName, "r")) EQ NULL)
  {
#endif
    fprintf(stderr, "ERR - Unable to open file [%s] %d (%s)\n", fName, errno,
            strerror(errno));
    return (EXIT_FAILURE);
  }

  while (((rCount = fread(inBuf, 1, sizeof(inBuf), inFile)) > 0) && (searchHead != NULL))
  {
#ifdef DEBUG
    if (config->debug >= 7)
      fprintf(stderr, "DEBUG - Read [%lu] bytes\n", rCount);
#endif

    sol = inBuf;
    rLeft = rCount;
    while (rLeft && (searchHead != NULL) && ((eol = strchr(sol, '\n')) != NULL))
    {

#ifdef DEBUG
      if (config->debug >= 5)
        fprintf(stderr, "DEBUG - Index record found\n");
#endif

      /* copy bytes (sol to eol) to lineBuf */
      lineBufSize += (eol - sol);
#ifdef DEBUG
      if (config->debug >= 6)
        fprintf(stderr, "DEBUG - Line Buf: [%lu]\n", lineBufSize);
#endif

      if ((lineBuf = XREALLOC(lineBuf, lineBufSize + 1)) EQ NULL)
      {
        fprintf(stderr,
                "ERR - Unable to allocate memory for index buffer [%lu]\n",
                lineBufSize);
        exit(EXIT_FAILURE);
      }
      XMEMCPY(lineBuf + linePos, sol, eol - sol);
      lineBuf[lineBufSize] = '\0';

#ifdef DEBUG
      if (config->debug >= 9)
        printf("%s\n", lineBuf);
#endif

      /* process line */
      if ( ( tok = strtok(lineBuf, ",") ) EQ NULL ) {
        fprintf( stderr, "ERR - Index may be corrupt\n" );
        exit( EXIT_FAILURE );
      }
      
#ifdef DEBUG
      if (config->debug >= 2)
        fprintf(stderr, "TOK: %s\n", tok);
#endif

      /* search until term is found */
      searchPtr = searchHead;
      while (searchPtr != NULL)
      {
        count = 0;
        if (searchPtr->len EQ strlen(tok))
        {
          if (XMEMCMP(searchPtr->term, tok,
                      searchPtr->len) EQ 0)
          {
            count = strtoll(strtok(NULL, ","), &endPtr, 10);

            config->match_offsets =
                XREALLOC(config->match_offsets,
                         (config->match_count + count + 1) * sizeof(size_t));
            fprintf(stderr, "MATCH [%s] with %zu lines\n", lineBuf, count);
            for (a = config->match_count; a < (config->match_count + count);
                 a++)
            {
              if ((tok = strtok(NULL, ",")) != NULL)
                config->match_offsets[a] = strtoll(tok, &endPtr, 10);
              else
              {
                fprintf(stderr, "ERR - Index is corrupt [%s]\n", lineBuf);
                exit(EXIT_FAILURE);
              }
            }
            config->match_count += count;
            match++;
          }
        }

        /* if we got a hit */
        if (count)
        {
#ifdef DEBUG
          if (config->debug >= 3)
            fprintf(stderr, "DEBUG - Removing matched term\n");
#endif
          /* matched the term, remove it from the list */
          if ((searchPtr->prev EQ NULL) && (searchPtr->next EQ NULL))
          {
            /* just one record left in the list */
#ifdef DEBUG
            if (config->debug >= 2)
              fprintf(stderr, "DEBUG - Removing last search term\n");
#endif
            searchHead = searchTail = NULL;
            XFREE(searchPtr->term);
            XFREE(searchPtr);
            searchPtr = NULL;
          }
          else if (searchPtr->next EQ NULL)
          {
            /* end of list */
            searchPtr->prev->next = NULL;
            searchTail = searchPtr->prev;
            XFREE(searchPtr->term);
            XFREE(searchPtr);
            searchPtr = NULL;
          }
          else if (searchPtr->prev EQ NULL)
          {
            /* begining of list */
            searchPtr->next->prev = NULL;
            searchHead = searchPtr->next;
            tmpPtr = searchPtr;
            searchPtr = searchPtr->next;
            XFREE(tmpPtr->term);
            XFREE(tmpPtr);
          }
          else
          {
            /* middle of list */
            searchPtr->next->prev = searchPtr->prev;
            searchPtr->prev->next = searchPtr->next;
            tmpPtr = searchPtr;
            searchPtr = searchPtr->next;
            XFREE(tmpPtr->term);
            XFREE(tmpPtr);
          }
        }
        else
          searchPtr = searchPtr->next; /* advance to next record */
      }

      /* reset lineBuf */
      rLeft -= (eol - sol) + 1;
      sol = eol + 1;
      lineBufSize = 0;
      linePos = 0;
      XFREE(lineBuf);
      lineBuf = NULL;
    }

    if (rLeft)
    {
#ifdef DEBUG
      if (config->debug >= 3)
        fprintf(stderr, "Overflow [%lu] bytes saved\n", rLeft);
#endif
      /* copy remainder from sol to end of inBuf to lineBuf */
      lineBufSize += rLeft;
      if ((lineBuf = XREALLOC(lineBuf, lineBufSize + 1)) EQ NULL)
      {
        fprintf(stderr,
                "ERR - Unable to allocate memory for index buffer [%lu]\n",
                lineBufSize);
        exit(EXIT_FAILURE);
      }
      XMEMCPY(lineBuf + linePos, sol, rLeft);
      linePos += rLeft;
      lineBuf[lineBufSize] = '\0';
    }
  }

#ifdef DEBUG
  if (config->debug >= 9)
  {
    for (a = 0; a < config->match_count; a++)
      printf("COUNT[%lu] LINE[%lu]\n", a, config->match_offsets[a]);
  }
  fflush(stdout);
#endif

#ifdef DEBUG
  if (config->debug >= 4)
  {
    if (linePos > 0)
    {
      fprintf(stderr, "DEBUG - Extra [%s]\n", lineBuf);
    }
  }
#endif

  /* sort the offset list */
  if (config->match_count > 1)
  {

#ifdef DEBUG
    if (config->debug >= 4)
      fprintf(stderr, "DEBUG - Match count: %lu\n", config->match_count);
#endif

    bubbleSort(config->match_offsets, config->match_count);
  }

  fclose(inFile);

  /* cleanup temp search term list */
  while ( searchHead != NULL ) {
    tmpPtr = searchHead;
    searchHead = searchHead->next;
    XFREE( tmpPtr->term );
    XFREE( tmpPtr );
  }
  searchTail = searchHead = NULL;

  if (match)
    return (EXIT_SUCCESS);
  else
    return (EXIT_FAILURE);
}

/****
 *
 * bubble sort the offset array
 *
 ****/

void bubbleSort(size_t list[], size_t n)
{
  size_t c, d, t;

  for (c = 0; c < n - 1; c++)
  {
    for (d = 0; d < n - c - 1; d++)
    {
      if (list[d] > list[d + 1])
      {
        /* Swapping */
        t = list[d];
        list[d] = list[d + 1];
        list[d + 1] = t;
      }
    }
  }
}

/****
 *
 * load search terms from file
 *
 ****/

int loadSearchFile(const char *fName)
{
  FILE *inFile = NULL;
  char inBuf[8192];
  int i = 0;
  struct searchTerm_s *searchPtr;

  fprintf(stderr, "Opening [%s] for read\n", fName);
#ifdef HAVE_FOPEN64
  if ((inFile = fopen64(fName, "r")) EQ NULL)
  {
#else
  if ((inFile = fopen(fName, "r")) EQ NULL)
  {
#endif
    fprintf(stderr, "ERR - Unable to open file [%s] %d (%s)\n", fName, errno,
            strerror(errno));
    return (EXIT_FAILURE);
  }

  while (fgets(inBuf, sizeof(inBuf), inFile) != NULL)
  {
    /* create new search term record */
    if ((searchPtr = XMALLOC(sizeof(struct searchTerm_s))) EQ NULL)
    {
      fprintf(stderr, "ERR - Unable to allocate memory for search term\n");
      exit(EXIT_FAILURE);
    }
    XMEMSET(searchPtr, '\0', sizeof(struct searchTerm_s));
    searchPtr->len = strlen(inBuf);
    searchPtr->term = XMALLOC(searchPtr->len+1);
    XMEMCPY(searchPtr->term, inBuf, searchPtr->len);
    if (searchPtr->term[searchPtr->len - 1] EQ '\n') {
      searchPtr->term[searchPtr->len - 1] = '\0';
      searchPtr->len--;
    }

    /* store search term in the linked list */
    searchPtr->next = config->searchHead;
    config->searchHead = searchPtr;
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

int processFile(const char *fName)
{
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
  if (strcmp(fName, "-") EQ 0)
  {
    inFile = stdin;
  }
  else
  {
#ifdef HAVE_FOPEN64
    if ((inFile = fopen64(fName, "r")) EQ NULL)
    {
#else
    if ((inFile = fopen(fName, "r")) EQ NULL)
    {
#endif
      fprintf(stderr, "ERR - Unable to open file [%s] %d (%s)\n", fName, errno,
              strerror(errno));
      return (EXIT_FAILURE);
    }
  }

  while (fgets(inBuf, sizeof(inBuf), inFile) != NULL && !quit)
  {
    if (reload EQ TRUE)
    {
      fprintf(stderr, "Processed %d lines/min\n", lineCount);
#ifdef DEBUG
      if (config->debug)
      {
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
    if (config->debug)
    {
      lineLen = strlen(inBuf);
      totLineLen += lineLen;
      if (lineLen < minLineLen)
        minLineLen = lineLen;
      else if (lineLen > maxLineLen)
        maxLineLen = lineLen;
    }

    if (config->debug >= 3)
      printf("DEBUG - Before [%s]", inBuf);
#endif

    if ((ret = parseLine(inBuf)) > 0)
    {

#ifdef DEBUG
      if (config->debug)
      {
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
  if (config->debug)
  {
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
