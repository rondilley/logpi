/*****
 *
 * Description: Log Pseudo Templater Headers
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

#ifndef LOGPI_DOT_H
#define LOGPI_DOT_H

/****
 *
 * defines
 *
 ****/

#define LINEBUF_SIZE 4096

/****
 *
 * includes
 *
 ****/

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include <sysdep.h>

#ifndef __SYSDEP_H__
# error something is messed up
#endif

#include <common.h>
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

struct Address_s {
  size_t line;
  size_t offset;
  struct Address_s *next;
};

typedef struct {
  size_t count;
  struct Address_s *head;
} metaData_t;

/****
 *
 * function prototypes
 *
 ****/

int printAddress( const struct hashRec_s *hashRec );
int processFile( const char *fName );
int showAddresses( void );

#endif /* LOGPI_DOT_H */

