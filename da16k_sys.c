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

/* Converts a double to a string, exists because on embedded we cannot be sure %f is supported */
bool da16k_double_to_string(char *buf, size_t buf_size, volatile double value) {
    long        integer             = (long) value;
    int         chars_written       = buf ? snprintf(buf, buf_size, "%ld.", integer) : 0;
    char       *decimal_ptr         = buf + (size_t) chars_written;
    const char *upper_bound         = buf + buf_size - 1;

    DA16K_RETURN_ON_NULL(false, buf);

    /* need to at least be able to fit 4 digits, e.g. -1.0 + null terminator*/
    if (chars_written <= 0 || buf_size < 5) {
        return false;
    }

    /* Get absolute value because we don't need the sign anymore; simplifies adjustments below */
    if (value < 0.0) {
        value *= -1.0;
        integer *= -1;
    }

    /* Write decimal part, up to 8 chars */

    for (size_t i = 0; i < 8; ++i) {
        if (decimal_ptr >= upper_bound) {
            if (i > 0) {
                /* We have some digits, we can truncate */
                break;
            } else {
                /* Catastrophic failure, abort. */
                return false;
            }
        }

        value = (value - (double) integer) * (double) 10;
        integer = (long) value;
        *decimal_ptr = '0' + (char) integer;
        decimal_ptr++;
        *decimal_ptr = 0x00;
    }

    /* Trim all the trailing zeroes. decimal_ptr points to the null terminator at this moment. */

    decimal_ptr--;

    while ((decimal_ptr > buf) && decimal_ptr[0] == '0') {
        *decimal_ptr = 0x00;
        decimal_ptr--;
    }

    /* Don't make it end with a period (e.g. "1.")*/

    if (decimal_ptr[0] == '.') {
        decimal_ptr[1] = '0';
    }

    return true;
}


