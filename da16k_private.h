/*
 * da16k_private.h
 *
 * Functionality for library-internal use
 *
 *  Created on: July 28, 2024
 *      Author: evoirin
 */

#ifndef DA16K_COMM_DA16K_PRIVATE_H_
#define DA16K_COMM_DA16K_PRIVATE_H_

#include "da16k_comm.h"

/* Helper macro to cleanly return a meaningful error on NULL whilst informing user properly */
#define DA16K_RETURN_ON_NULL(return_value, ptr) if (ptr == NULL) { DA16K_PRINT("%s: ERROR - " #ptr " is NULL!\r\n", __func__); return return_value; }

/* System & Utilities (da16k_sys.c) */

void   *da16k_malloc            (size_t size);
void    da16k_free              (void *ptr);
char   *da16k_strdup            (const char *src);
bool    da16k_double_to_string  (char *buf, size_t buf_size, volatile double value);

/* internal AT protocol functionality (da16k_at.c) */

/*  Wait for, receive and validate an AT response with a given timeout in milliseconds.

    Example for the following call:
        da16k_at_receive_and_validate_response(buf, buf_size, true, "+NWICGETCMD:");
    with the response: 
        > +NWICGETCMD:set_red_led on
        > OK
    would put "set_red_led on" into the internal response buffer. The same call with the response:
        > ERROR:7
        > OK
    would put "7" into the internal response buffer. 
    
    On DA16K_SUCCESS, the response can then be obtained either as a string or integer. */
da16k_err_t da16k_at_receive_and_validate_response          (bool error_possible, const char *expected_response, uint32_t timeout_ms);
/*  Send a printf-style formatted string to the DA16K module. This string would contain a valid AT command of some sort. 
    
    The caller must receive and validate the response using da16k_at_receive_and_validate_response. */
da16k_err_t da16k_at_send_formatted_msg                     (const char *format, ...);
/*  AT Commands that get a simple +EXAMPLE:<x> response and <x> is expected to be 1 for success
    can use this wrapper to do everything in a single function call to aid readability and code deduplication.

    The repsonse does not to be validated or retreived by the caller.

    returns DA16K_SUCCESS if the command was sent out successfully, the response was proper and had a return code of 1. */
da16k_err_t da16k_at_send_formatted_and_check_success_code  (uint32_t timeout_ms, const char *expected_response, const char *format, ...);
/*  Copy out the full, final, parsed response string into a new buffer. Will allocate. 
    WARNING: Only call this after a previous call to send a message yielded success. */
char       *da16k_at_get_response_str                       (void);
/*  Fetches an integer return code from the full, final, parsed response.
    WARNING: Only call this after a previous call to send a message yielded success. */
int         da16k_at_get_response_code                      (void);



#endif /* DA16K_COMM_DA16K_PRIVATE_H_ */