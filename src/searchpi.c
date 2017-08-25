/*****
 *
 * Description: Log Pseudo Indexer Functions
 * 
 * Copyright (c) 2008-2017, Ron Dilley
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
 * process file
 *
 ****/

int processFile( const char *fName ) {
  FILE *inFile = NULL, *outFile = NULL;
  char inBuf[8192];
  char outFileName[PATH_MAX];
  char patternBuf[4096];
  char oBuf[4096];
  PRIVATE int c = 0, i, ret;
  unsigned int totLineCount = 0, lineCount = 0, lineLen = 0, minLineLen = sizeof(inBuf), maxLineLen = 0, totLineLen = 0;
  unsigned int argCount = 0, totArgCount = 0, minArgCount = MAX_FIELD_POS, maxArgCount = 0;
  struct hashRec_s *tmpRec;
  metaData_t *tmpMd;
  struct Address_s *tmpAddr;
  struct Fields_s **curFieldPtr;

  /* initialize the hash if we need to */
  if ( addrHash EQ NULL )
    addrHash = initHash( 96 );

  initParser();

  fprintf( stderr, "Opening [%s] for read\n", fName );
  if ( strcmp( fName, "-" ) EQ 0 ) {
    inFile = stdin;
  } else {
#ifdef HAVE_FOPEN64
    if ( ( inFile = fopen64( fName, "r" ) ) EQ NULL ) {
#else
    if ( ( inFile = fopen( fName, "r" ) ) EQ NULL ) {
#endif
      fprintf( stderr, "ERR - Unable to open file [%s] %d (%s)\n", fName, errno, strerror( errno ) );
      return( EXIT_FAILURE );
    }
  }

  while( fgets( inBuf, sizeof( inBuf ), inFile ) != NULL && ! quit ) {
    if ( reload EQ TRUE ) {
      fprintf( stderr, "Processed %d lines/min\n", lineCount );
#ifdef DEBUG
      if ( config->debug ) {
	fprintf( stderr, "Line length: min=%d, max=%d, avg=%2.0f\n", minLineLen, maxLineLen, (float)totLineLen / (float)lineCount );

	minLineLen = sizeof(inBuf);
	maxLineLen = 0;
	totLineLen = 0;
      }
#endif
      lineCount = 0;
      reload = FALSE;
    }

#ifdef DEBUG
    if ( config->debug ) {
      lineLen = strlen( inBuf );
      totLineLen += lineLen;
      if ( lineLen < minLineLen )
	minLineLen = lineLen;
      else if ( lineLen > maxLineLen )
	maxLineLen = lineLen;
    }
#endif

    if ( config->debug >= 3 )
      printf( "DEBUG - Before [%s]", inBuf );

    if ( ( ret = parseLine( inBuf ) ) > 0 ) {

#ifdef DEBUG
      if ( config->debug ) {
	/* save arg count */
	totArgCount += ret;
	if ( ret < minArgCount )
	  minArgCount = ret;
	else if ( ret > maxArgCount )
	  maxArgCount = ret;
      }
#endif

      lineCount++;
      totLineCount++;
    }
  }
  
#ifdef DEBUG
  if ( config->debug ) {
    fprintf( stderr, "Line length: min=%d, max=%d, avg=%2.0f\n", minLineLen, maxLineLen, (float)totLineLen / (float)lineCount );
      
    minLineLen = sizeof(inBuf);
    maxLineLen = 0;
    totLineLen = 0;
  }
#endif
  
  if ( inFile != stdin )  
    fclose( inFile );
    
  deInitParser();

  return( EXIT_SUCCESS );
}
