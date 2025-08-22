/*****
 *
 * Description: High-Performance Network Address Parser Implementation
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

#include "netaddr_parser.h"
#include <string.h>
#include <stdio.h>

/* SSE includes for SIMD operations */
#ifdef __SSE4_2__
#include <immintrin.h>
#endif

/****
 *
 * Hex digit lookup table
 *
 ****/

const uint8_t hex_table[256] ALIGNED(64) = {
    [0 ... 255] = 0xFF,  /* Invalid by default */
    ['0'] = 0, ['1'] = 1, ['2'] = 2, ['3'] = 3,
    ['4'] = 4, ['5'] = 5, ['6'] = 6, ['7'] = 7,
    ['8'] = 8, ['9'] = 9,
    ['A'] = 10, ['B'] = 11, ['C'] = 12, ['D'] = 13, ['E'] = 14, ['F'] = 15,
    ['a'] = 10, ['b'] = 11, ['c'] = 12, ['d'] = 13, ['e'] = 14, ['f'] = 15
};

/****
 *
 * SIMD-optimized scanning
 *
 ****/

#ifdef __SSE4_2__

/* Find all dots in string using SIMD */
static ALWAYS_INLINE int simd_scan_for_dots(const char* str, size_t len, size_t* positions) {
    __m128i dot = _mm_set1_epi8('.');
    int count = 0;
    size_t pos = 0;
    
    /* Process 16 bytes at a time */
    while (pos + 16 <= len && count < 64) {
        __m128i chunk = _mm_loadu_si128((const __m128i*)(str + pos));
        __m128i cmp = _mm_cmpeq_epi8(chunk, dot);
        int mask = _mm_movemask_epi8(cmp);
        
        while (mask && count < 64) {
            int bit = __builtin_ffs(mask) - 1;
            positions[count++] = pos + bit;
            mask &= ~(1 << bit);
        }
        pos += 16;
    }
    
    /* Handle remaining bytes */
    while (pos < len && count < 64) {
        if (str[pos] == '.') {
            positions[count++] = pos;
        }
        pos++;
    }
    
    return count;
}

/* Find all colons in string using SIMD */
static ALWAYS_INLINE int simd_scan_for_colons(const char* str, size_t len, size_t* positions) {
    __m128i colon = _mm_set1_epi8(':');
    int count = 0;
    size_t pos = 0;
    
    while (pos + 16 <= len && count < 64) {
        __m128i chunk = _mm_loadu_si128((const __m128i*)(str + pos));
        __m128i cmp = _mm_cmpeq_epi8(chunk, colon);
        int mask = _mm_movemask_epi8(cmp);
        
        while (mask && count < 64) {
            int bit = __builtin_ffs(mask) - 1;
            positions[count++] = pos + bit;
            mask &= ~(1 << bit);
        }
        pos += 16;
    }
    
    /* Handle remaining bytes */
    while (pos < len && count < 64) {
        if (str[pos] == ':' || str[pos] == '-') {
            positions[count++] = pos;
        }
        pos++;
    }
    
    return count;
}

#endif /* __SSE4_2__ */

/****
 *
 * Fast IPv4 extraction
 *
 ****/

static ALWAYS_INLINE int fast_extract_ipv4(const char* str, size_t max_len, net_addr_t* addr) {
    const char* ptr = str;
    const char* start = str;
    uint32_t octets[4];
    int octet_count = 0;
    int digits = 0;
    uint32_t value = 0;
    
    /* Quick pre-check: need at least 7 chars for shortest IPv4 */
    if (max_len < 7) return 0;
    
    while (ptr - start < max_len && ptr - start < 15) {
        char c = *ptr;
        
        if (IS_DIGIT(c)) {
            value = value * 10 + (c - '0');
            digits++;
            
            /* Each octet can have max 3 digits */
            if (digits > 3 || value > 255) {
                return 0;
            }
        } else if (c == '.') {
            if (digits == 0 || octet_count >= 3) {
                return 0;
            }
            octets[octet_count++] = value;
            value = 0;
            digits = 0;
        } else {
            /* End of potential IP */
            break;
        }
        ptr++;
    }
    
    /* Check last octet */
    if (octet_count == 3 && digits > 0 && value <= 255) {
        octets[3] = value;
        
        /* Valid IPv4 found */
        addr->type = ADDR_TYPE_IPV4;
        addr->offset = start - str;
        addr->length = ptr - start;
        addr->addr.ipv4 = htonl((octets[0] << 24) | (octets[1] << 16) | 
                                (octets[2] << 8) | octets[3]);
        
        /* Store string representation */
        snprintf(addr->str, sizeof(addr->str), "%u.%u.%u.%u",
                octets[0], octets[1], octets[2], octets[3]);
        
        return ptr - start;
    }
    
    return 0;
}

/****
 *
 * Fast IPv6 extraction
 *
 ****/

static ALWAYS_INLINE int fast_extract_ipv6(const char* str, size_t max_len, net_addr_t* addr) {
    const char* ptr = str;
    const char* start = str;
    uint16_t groups[8] = {0};
    int group_count = 0;
    int digits = 0;
    uint32_t value = 0;
    int double_colon = -1;
    int i;
    
    /* Quick pre-check: need at least 3 chars for shortest IPv6 */
    if (max_len < 3) return 0;
    
    while (ptr - start < max_len && ptr - start < 39) {
        char c = *ptr;
        
        if (IS_HEX(c)) {
            value = (value << 4) | HEX_VALUE(c);
            digits++;
            
            if (digits > 4) {
                return 0;  /* Too many hex digits */
            }
        } else if (c == ':') {
            if (ptr[1] == ':') {
                /* Double colon */
                if (double_colon >= 0) {
                    return 0;  /* Only one :: allowed */
                }
                if (digits > 0) {
                    groups[group_count++] = value;
                }
                double_colon = group_count;
                value = 0;
                digits = 0;
                ptr++;  /* Skip second colon */
            } else {
                /* Single colon */
                if (digits == 0 && group_count == 0) {
                    return 0;  /* Can't start with single : */
                }
                if (group_count >= 8) {
                    return 0;  /* Too many groups */
                }
                groups[group_count++] = value;
                value = 0;
                digits = 0;
            }
        } else if (c == '.' && group_count >= 6) {
            /* IPv4-mapped IPv6 address (e.g., ::ffff:192.168.1.1) */
            /* Try to parse IPv4 part */
            const char* ipv4_start = ptr - digits;
            net_addr_t ipv4_addr;
            int ipv4_len = fast_extract_ipv4(ipv4_start, max_len - (ipv4_start - start), &ipv4_addr);
            if (ipv4_len > 0) {
                /* Convert IPv4 to last two IPv6 groups */
                uint32_t ipv4_val = ntohl(ipv4_addr.addr.ipv4);
                groups[group_count++] = (ipv4_val >> 16) & 0xFFFF;
                groups[group_count++] = ipv4_val & 0xFFFF;
                ptr = ipv4_start + ipv4_len;
                goto ipv6_complete;
            }
            return 0;
        } else {
            /* End of IPv6 */
            break;
        }
        ptr++;
    }
    
    /* Add last group if any */
    if (digits > 0) {
        if (group_count >= 8) {
            return 0;
        }
        groups[group_count++] = value;
    }
    
ipv6_complete:
    /* Validate IPv6 */
    if (double_colon >= 0) {
        /* Handle compressed notation */
        if (group_count > 8) {
            return 0;
        }
        /* Valid compressed IPv6 */
    } else {
        /* Full notation must have exactly 8 groups */
        if (group_count != 8) {
            return 0;
        }
    }
    
    /* Store IPv6 address */
    addr->type = ADDR_TYPE_IPV6;
    addr->offset = start - str;
    addr->length = ptr - start;
    
    /* Convert to binary representation */
    memset(addr->addr.ipv6, 0, 16);
    if (double_colon >= 0) {
        /* Copy groups before :: */
        for (i = 0; i < double_colon; i++) {
            addr->addr.ipv6[i*2] = (groups[i] >> 8) & 0xFF;
            addr->addr.ipv6[i*2+1] = groups[i] & 0xFF;
        }
        /* Copy groups after :: from the end */
        int after = group_count - double_colon;
        for (i = 0; i < after; i++) {
            addr->addr.ipv6[16 - (after-i)*2] = (groups[double_colon+i] >> 8) & 0xFF;
            addr->addr.ipv6[16 - (after-i)*2 + 1] = groups[double_colon+i] & 0xFF;
        }
    } else {
        /* Full notation */
        for (i = 0; i < 8; i++) {
            addr->addr.ipv6[i*2] = (groups[i] >> 8) & 0xFF;
            addr->addr.ipv6[i*2+1] = groups[i] & 0xFF;
        }
    }
    
    /* Store string representation */
    memcpy(addr->str, start, ptr - start);
    addr->str[ptr - start] = '\0';
    
    return ptr - start;
}

/****
 *
 * Fast MAC address extraction
 *
 ****/

static ALWAYS_INLINE int fast_extract_mac(const char* str, size_t max_len, net_addr_t* addr) {
    const char* ptr = str;
    const char* start = str;
    uint8_t bytes[6];
    int byte_count = 0;
    int digits = 0;
    uint32_t value = 0;
    char separator = 0;
    
    /* MAC needs exactly 17 chars (XX:XX:XX:XX:XX:XX) */
    if (max_len < 17) return 0;
    
    while (ptr - start < 17 && byte_count < 6) {
        char c = *ptr;
        
        if (IS_HEX(c)) {
            value = (value << 4) | HEX_VALUE(c);
            digits++;
            
            if (digits > 2) {
                return 0;  /* Too many hex digits */
            }
        } else if ((c == ':' || c == '-') && digits == 2) {
            if (separator == 0) {
                separator = c;  /* Remember first separator */
            } else if (c != separator) {
                return 0;  /* Mixed separators */
            }
            
            if (byte_count >= 6) {
                return 0;  /* Too many bytes */
            }
            
            bytes[byte_count++] = value;
            value = 0;
            digits = 0;
        } else {
            /* Not a valid MAC character */
            if (byte_count == 5 && digits == 2) {
                /* Last byte */
                bytes[5] = value;
                byte_count = 6;
                break;
            }
            return 0;
        }
        ptr++;
    }
    
    /* Check if we have a complete MAC */
    if (byte_count == 5 && digits == 2) {
        bytes[5] = value;
        byte_count = 6;
    }
    
    if (byte_count != 6) {
        return 0;
    }
    
    /* Valid MAC found */
    addr->type = ADDR_TYPE_MAC;
    addr->offset = start - str;
    addr->length = 17;
    memcpy(addr->addr.mac, bytes, 6);
    
    /* Store string representation */
    snprintf(addr->str, sizeof(addr->str), "%02x:%02x:%02x:%02x:%02x:%02x",
            bytes[0], bytes[1], bytes[2], bytes[3], bytes[4], bytes[5]);
    
    return 17;
}

/****
 *
 * Main parsing function
 *
 ****/

int parse_network_addresses(const char* line, size_t len, parse_result_t* result) {
    size_t pos = 0;
    size_t dot_positions[64];
    size_t colon_positions[64];
    int dot_count = 0;
    int colon_count = 0;
    
    if (UNLIKELY(!line || !result || len == 0)) {
        return 0;
    }
    
    /* Clear result */
    memset(result, 0, sizeof(*result));
    
    /* Prefetch line data for better cache performance */
    PREFETCH_R(line);
    PREFETCH_R(line + 64);
    PREFETCH_R(line + 128);
    
#ifdef __SSE4_2__
    /* Use SIMD to find all dots and colons quickly */
    dot_count = simd_scan_for_dots(line, len, dot_positions);
    colon_count = simd_scan_for_colons(line, len, colon_positions);
#else
    /* Fallback: scan for dots and colons */
    for (pos = 0; pos < len; pos++) {
        if (line[pos] == '.' && dot_count < 64) {
            dot_positions[dot_count++] = pos;
        } else if ((line[pos] == ':' || line[pos] == '-') && colon_count < 64) {
            colon_positions[colon_count++] = pos;
        }
    }
#endif
    
    /* Try to extract IPv4 addresses near dots */
    for (int i = 0; i < dot_count && result->count < 256; i++) {
        size_t dot_pos = dot_positions[i];
        
        /* Scan backwards to find start of potential IP */
        size_t start = dot_pos;
        while (start > 0 && (IS_DIGIT(line[start-1]) || line[start-1] == '.')) {
            start--;
        }
        
        /* Try to extract IPv4 */
        net_addr_t addr;
        int addr_len = fast_extract_ipv4(line + start, len - start, &addr);
        if (addr_len > 0) {
            addr.offset = start;
            result->addresses[result->count++] = addr;
            result->ipv4_count++;
            
            /* Skip ahead to avoid re-parsing same IP */
            i++;
            while (i < dot_count && dot_positions[i] < start + addr_len) {
                i++;
            }
            i--;  /* Compensate for loop increment */
        }
    }
    
    /* Try to extract IPv6 and MAC addresses near colons */
    for (int i = 0; i < colon_count && result->count < 256; i++) {
        size_t colon_pos = colon_positions[i];
        
        /* Check if this position was already processed */
        int already_processed = 0;
        for (uint32_t j = 0; j < result->count; j++) {
            if (colon_pos >= result->addresses[j].offset &&
                colon_pos < result->addresses[j].offset + result->addresses[j].length) {
                already_processed = 1;
                break;
            }
        }
        if (already_processed) continue;
        
        /* Scan backwards to find start */
        size_t start = colon_pos;
        while (start > 0) {
            char c = line[start-1];
            if (IS_HEX(c) || c == ':' || c == '-') {
                start--;
            } else {
                break;
            }
        }
        
        /* Try MAC first (it's more specific - exactly 17 chars) */
        net_addr_t addr;
        int addr_len = fast_extract_mac(line + start, len - start, &addr);
        if (addr_len > 0) {
            addr.offset = start;
            result->addresses[result->count++] = addr;
            result->mac_count++;
            
            /* Skip colons in this MAC */
            while (i < colon_count && colon_positions[i] < start + addr_len) {
                i++;
            }
            i--;
            continue;
        }
        
        /* Try IPv6 */
        addr_len = fast_extract_ipv6(line + start, len - start, &addr);
        if (addr_len > 0) {
            addr.offset = start;
            result->addresses[result->count++] = addr;
            result->ipv6_count++;
            
            /* Skip colons in this IPv6 */
            while (i < colon_count && colon_positions[i] < start + addr_len) {
                i++;
            }
            i--;
        }
    }
    
    return result->count;
}

/****
 *
 * Initialize parser
 *
 ****/

void init_netaddr_parser(void) {
    /* Verify SIMD support if available */
#ifdef __SSE4_2__
    unsigned int eax, ebx, ecx, edx;
    __asm__ __volatile__ (
        "cpuid"
        : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
        : "a"(1)
    );
    
    if (!(ecx & (1 << 20))) {
        fprintf(stderr, "WARNING: SSE4.2 not supported on this CPU\n");
    }
#endif
}