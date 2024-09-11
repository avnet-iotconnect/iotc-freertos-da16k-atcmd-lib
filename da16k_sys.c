/*
 * da16k_sys.c
 *
 *  Created on: July 28, 2024
 *      Author: evoirin
 *
 * IoTConnect via Dialog DA16K module - internal system functions and utilities.
 */

#include "da16k_private.h"

#include <stdio.h>
#include <unistd.h>
#include <stdint.h>
#include <limits.h>

/* Wrappers for external functions that may be unreliable / redefined */

void *da16k_malloc(size_t size) {
    return DA16K_CONFIG_MALLOC_FN(size);
}

void da16k_free(void *ptr) {
    if (ptr != NULL) {  /* some non-compliant C libraries may crash on freeing NULL pointers... */
        DA16K_CONFIG_FREE_FN(ptr);
    }
}

char *da16k_strdup(const char *src) {
    size_t str_size = strlen(src) + 1; /* + 1 for null terminator */
    char *ret = da16k_malloc(str_size);

    if (ret) {
        memcpy(ret, src, str_size);
    }

    return ret;
}

char *da16k_strndup(const char *src, size_t size) {
    size_t str_size = size + 1; /* + 1 for null terminator */
    char *ret = da16k_malloc(str_size);

    if (ret) {
        memcpy(ret, src, str_size);
        ret[size] = 0x00;
    }

    return ret;
}

/*  Converts a set of bytes to an ascii hex representation for use in the DA16K AT protocol
    The protocol is BIG ENDIAN, so on little endian systems the endianness will be swapped in the output. */
static bool da16k_bytes_to_ascii_hex(char *dst, void *src, size_t length) {
    const uint8_t *c_src = (const uint8_t *) src;

    DA16K_RETURN_ON_NULL(false, src);
    DA16K_RETURN_ON_NULL(false, dst);

    if (length > INT_MAX) {
        return false;
    }

#if defined(__ATCMD_LITTLE_ENDIAN__)
    for (size_t i = length; i > 0; i--) {
        sprintf(dst, "%02x", c_src[i-1]);
        dst += 2;
    }
#elif defined(__ATCMD_BIG_ENDIAN__)
    for (size_t i = 0; i < length; i++) {
        sprintf(dst, "%02x", c_src[i]);
        dst += 2;
    }
#else
    #error "Endianness unknown. Please define __ATCMD_LITTLE_ENDIAN__ or __ATCMD_BIG_ENDIAN__."
#endif

    return true;
}

bool da16k_bool_to_ascii_hex (char *dst, bool value) {
    uint8_t temp = value ? 0x01 : 0x00;
    return da16k_bytes_to_ascii_hex(dst, (void *) &temp, sizeof(uint8_t));
}

bool da16k_double_to_ascii_hex (char *dst, double value) {
    return da16k_bytes_to_ascii_hex(dst, (void *) &value, sizeof(double));
}
