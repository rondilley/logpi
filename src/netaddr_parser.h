/*****
 *
 * Description: High-Performance Network Address Parser
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

#ifndef NETADDR_PARSER_DOT_H
#define NETADDR_PARSER_DOT_H

/****
 *
 * includes
 *
 ****/

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include "../include/sysdep.h"
#include "../include/common.h"
#include <stdint.h>
#include <immintrin.h>
#include <arpa/inet.h>

/****
 *
 * Performance macros
 *
 ****/

#define CACHE_LINE      64

/* Prefetch for read */
#define PREFETCH_R(addr) __builtin_prefetch((addr), 0, 3)

/****
 *
 * Network address types
 *
 ****/

#define ADDR_TYPE_IPV4  1
#define ADDR_TYPE_IPV6  2
#define ADDR_TYPE_MAC   3

/****
 *
 * Fast character classification for network addresses
 *
 ****/

/* Lookup table for hex digits - 256 bytes */
extern const uint8_t hex_table[256] ALIGNED(64);

/* Fast digit/hex checks */
#define IS_DIGIT(c)     ((c) >= '0' && (c) <= '9')
#define IS_HEX(c)       (hex_table[(uint8_t)(c)] != 0xFF)
#define HEX_VALUE(c)    (hex_table[(uint8_t)(c)])

/****
 *
 * Network address structure
 *
 ****/

typedef struct net_addr {
    uint8_t type;           /* ADDR_TYPE_* */
    uint16_t offset;        /* Offset in line */
    uint16_t length;        /* Length of address */
    union {
        uint32_t ipv4;      /* IPv4 in network byte order */
        uint8_t ipv6[16];   /* IPv6 address */
        uint8_t mac[6];     /* MAC address */
    } addr;
    char str[40];           /* String representation */
} net_addr_t;

/****
 *
 * Parser results structure
 *
 ****/

typedef struct parse_result {
    net_addr_t addresses[256];  /* Found addresses */
    uint32_t count;             /* Number of addresses found */
    uint32_t ipv4_count;
    uint32_t ipv6_count;
    uint32_t mac_count;
} parse_result_t;

/****
 *
 * SIMD-optimized scanning functions
 *
 ****/

/* Note: Internal helper functions are defined as static inline in the .c file */

/****
 *
 * Main parsing function
 *
 ****/

/* Parse line for all network addresses */
int parse_network_addresses(const char* line, size_t len, parse_result_t* result);

/* Initialize parser (one-time setup) */
void init_netaddr_parser(void);

#endif /* NETADDR_PARSER_DOT_H */