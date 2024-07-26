/*
 * da16k_comm.c
 *
 *  Created on: Dec 14, 2023
 *      Author: evoirin
 *
 * IoTConnect via Dialog DA16K module.
 */

#include "da16k_comm.h"

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdarg.h>

#include <string.h>
#include <inttypes.h>

#include "da16k_uart.h"

#define DA16K_RETURN_ON_NULL(return_value, ptr) if (ptr == NULL) { DA16K_PRINT("%s: ERROR - " #ptr " is NULL!\r\n", __func__); return return_value; }

static char da16k_value_buffer[64] = {0};
static char da16k_at_send_buffer[256];
static char da16k_at_response_buffer[512];

/* Wrappers for external functions that may be unreliable / redefined */

static void *da16k_malloc(size_t size) {
    return DA16K_CONFIG_MALLOC_FN(size);
}

static void da16k_free(void *ptr) {
    if (ptr != NULL) {  /* some non-compliant C libraries may crash on freeing NULL pointers... */
        DA16K_CONFIG_FREE_FN(ptr);
    }
}

static char *da16k_strdup(const char *src) {
    size_t str_size = strlen(src) + 1; /* + 1 for null terminator */
    char *ret = da16k_malloc(str_size);

    if (ret) {
        memcpy(ret, src, str_size);
    }

    return ret;
}

/* Converts a double to a string, exists because on embedded we cannot be sure %f is supported */
static bool da16k_double_to_string(char *buf, size_t buf_size, volatile double value) {
    long        integer             = (long) value;
    int         chars_written       = buf ? snprintf(buf, buf_size, "%ld.", integer) : 0;
    char       *decimal_ptr         = buf + (size_t) chars_written;
    const char *upper_bound         = buf + buf_size - 1;

    DA16K_RETURN_ON_NULL(false, buf);

    /* need to at least be able to fit 3 digits, e.g. 1.0 + null terminator*/
    if (chars_written <= 0 || buf_size < 4) {
        return false;
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

/* Receive a line of AT response from UART with a timeout.

   Returns:
   DA16K_INVALID_PARAMETER - buf or buf_size are invalid.
   DA16K_SUCCESS - a line of text is in the buffer, EXCLUDING \r\n delimiter. 
   DA16K_AT_RESPONSE_TOO_LONG - buf is too small to accommodate the response received.
        The characters for this line will not be flushed in this case, the remainder can be fetched with another call to da16k_at_get_response_line
   DA16K_TIMEOUT - The specified timeout was reached before another character could be fetched. There may be response data in the buffer.

   The function will fetch at most buf_size - 1 characters and the retreived data is guaranteed to be null-terminated, even in case of errors.

   Other errors should not occur in normal operation. */
static da16k_err_t da16k_at_get_response_line(char *buf, size_t buf_size, uint32_t timeout_ms) {
    char       *write_ptr   = buf;
    char       *upper_bound = buf + buf_size - 1;
    char        last_char   = 0x00;
    da16k_err_t ret         = DA16K_SUCCESS;

    DA16K_RETURN_ON_NULL(DA16K_INVALID_PARAMETER, buf);

    if (buf_size == 0) {
        return DA16K_INVALID_PARAMETER;
    }

    memset(buf, 0, buf_size);

    while (true) {
        /* Buffer is full, return & inform */

        if (write_ptr >= upper_bound) {
            return DA16K_AT_RESPONSE_TOO_LONG;
        }

        /* Get next uart char */
        ret = da16k_uart_get_char(write_ptr, timeout_ms);

        if ((last_char == '\r') && (*write_ptr == '\n')) {
            /* We received a full line ended by \r\n */
            write_ptr--;
            write_ptr[0] = 0x00;    /* Replace \r */
            write_ptr[1] = 0x00;    /* Replace \n */
            return DA16K_SUCCESS;
        }

        /* Error */

        if (ret != DA16K_SUCCESS) {
            return ret;
        }

        last_char = *write_ptr;
        write_ptr++;
    }

    return ret;
}

/*  Gets a pointer to the start of AT response data following the colon character. 
    E.g. for ERROR:<x> or +SOMECOMMAND:<x>) it would return a pointer to <x> given the appropriate start_of_response character, if ERROR or +SOMECOMMAND are given in start_of_response.
    Returns NULL if not found or out of bounds.
*/
static char *da16k_at_get_start_of_response_data(char *buf, size_t buf_size, const char *start_of_response) {
    char *ret           = NULL;
    char *upper_bound   = buf + buf_size;

    DA16K_RETURN_ON_NULL(NULL, buf);
    DA16K_RETURN_ON_NULL(NULL, start_of_response);

    ret = strstr(buf, start_of_response);

    if (ret == NULL) {
        return NULL;
    }

    ret += strlen(start_of_response);
    
    /* Final check: colon character & bounds check */
    if (((ret + 1) >= upper_bound) || (*ret != ':')) {
        return NULL;
    }

    return ret + 1;
}


/*  Wait for, receive and validate an AT response with a given timeout in milliseconds.

    Returns the same error codes as da16k_at_get_response_line.

    Example for the following call:

    da16k_at_receive_and_validate_response(buf, buf_size, true, "+NWICGETCMD:");

    with the response: 

    > +NWICGETCMD:set_red_led on
    > OK

    would put "set_red_led on" into buf. The same call with the response:

    > ERROR:7
    > OK

    would put "7" into buf.
*/
static da16k_err_t da16k_at_receive_and_validate_response(char *buf, size_t buf_size, bool error_possible, const char *expected_response, uint32_t timeout_ms) {
    bool error_received         = false;
    bool ok_received            = false;

    char *upper_bound           = buf + buf_size;
    char *response_data_start   = NULL;

    da16k_err_t ret             = DA16K_SUCCESS;

    DA16K_RETURN_ON_NULL(DA16K_INVALID_PARAMETER, buf);

    if (buf_size < 16) {
        return DA16K_INVALID_PARAMETER;
    }
    
    while (ret == DA16K_SUCCESS) {
        ret = da16k_at_get_response_line(buf, buf_size, timeout_ms);

        if (ret == DA16K_AT_RESPONSE_TOO_LONG) {
            DA16K_PRINT("%s: WARNING! RX buffer overflow!\r\nRX Buffer contents:\r\n%s\r\n", __func__, buf);
        }


        /* Look for proper response */
        response_data_start = da16k_at_get_start_of_response_data(buf, buf_size, expected_response);

        /* If we can't find the expected response, but an ERROR:<x> is possible, flag and look for the error response */
        if (error_possible && (response_data_start == NULL)) {
            response_data_start = da16k_at_get_start_of_response_data(buf, buf_size, "ERROR");
            if (response_data_start != NULL) {
                error_received = true;
            }
        }

        /* Mark whether the OK\r\n part of the response was received. */
        if (strstr(buf, "OK") != NULL) {
            ok_received = true;
        }

        /* We received a valid response relevant to us, break */
        if (response_data_start) {
            break;
        }
    }

    if (response_data_start) {
        /* Move all response data to the start of the buffer; memmove means we don't need an intermediate buffer */
        memmove(buf, response_data_start, (size_t) (upper_bound - response_data_start));
        
        if (error_received) {
            ret = DA16K_AT_ERROR_CODE;  /* So caller can handle this case properly */
        } else if (ok_received) {
            ret = DA16K_SUCCESS;        /* Everything is OK */
        } else {
            ret = DA16K_AT_NO_OK;       /* "OK\r\n" was missing */
        }
    }
    
    /* In case of no response data, return last error code */
    return ret;
}

/*  analogous to vprintf, this is like da16k_at_send_formatted_msg but takes va_list as parameter to reduce
    code duplication for other funcs that allow formatted messages to be sent */
static da16k_err_t da16k_at_send_formatted_valist(const char *format, va_list args) {
    int at_msg_length;
    
    DA16K_RETURN_ON_NULL(DA16K_INVALID_PARAMETER, format);

    at_msg_length = vsnprintf(da16k_at_send_buffer, sizeof(da16k_at_send_buffer), format, args);

    if (at_msg_length < 0) {
        return DA16K_AT_INVALID_MSG;
    }

    /* + 2 for \r\n terminator*/
    if ((size_t) (at_msg_length + 2) >= sizeof(da16k_at_send_buffer)) { 
        return DA16K_AT_MESSAGE_TOO_LONG;
    }

    /* Add \r\n to terminate the message */
    at_msg_length += sprintf(&da16k_at_send_buffer[at_msg_length], "\r\n");

    DA16K_PRINT("%s -> %s", __func__, da16k_at_send_buffer);

    return da16k_uart_send(da16k_at_send_buffer, (size_t) at_msg_length) ? DA16K_SUCCESS : DA16K_UART_ERROR;
}

/*  Send a printf-style formatted string to the DA16K module. This string would contain a valid AT command of some sort. */
static da16k_err_t da16k_at_send_formatted_msg(const char *format, ...) {
    va_list args;
    da16k_err_t ret;

    DA16K_RETURN_ON_NULL(DA16K_INVALID_PARAMETER, format);

    va_start(args, format);
    ret = da16k_at_send_formatted_valist(format, args);
    va_end(args);

    return ret;
 }

/*  AT Commands that get a simple +EXAMPLE:<x> response and <x> is expected to be 1 for success
    can use this wrapper to do everything in a single function call to aid readability and code deduplication.
    returns DA16K_SUCCESS if the command was sent out successfully, the response was proper and had a return code of 1. */
static da16k_err_t da16k_at_send_formatted_and_check_success_code(uint32_t timeout_ms, const char *expected_response, const char *format, ...) {
    da16k_err_t ret = DA16K_SUCCESS;
    va_list fmt_args;

    DA16K_RETURN_ON_NULL(DA16K_INVALID_PARAMETER, expected_response);
    DA16K_RETURN_ON_NULL(DA16K_INVALID_PARAMETER, format);

    va_start(fmt_args, format);
    ret = da16k_at_send_formatted_valist(format, fmt_args);
    va_end(fmt_args);

    if (ret != DA16K_SUCCESS) {
        DA16K_PRINT("%s: Error sending message: %d\r\n", __func__, (int) ret);
        return ret;
    }

    ret = da16k_at_receive_and_validate_response(da16k_at_response_buffer, sizeof(da16k_at_response_buffer), false, expected_response, timeout_ms);

    if (ret == DA16K_SUCCESS && atoi(da16k_at_response_buffer) != 1) {
        DA16K_PRINT("%s: AT command not successful; error response: %s\r\n", __func__, da16k_at_response_buffer);
        ret = DA16K_AT_FAIL;
    }

    return ret;
}

da16k_err_t da16k_get_cmd(da16k_cmd_t *cmd) {
    const char  expected_response[] = "+NWICGETCMD";
    const char  at_message[]        = "AT+NWICGETCMD";
    char       *param_ptr           = NULL;
    da16k_err_t ret                 = DA16K_SUCCESS;

    DA16K_RETURN_ON_NULL(DA16K_INVALID_PARAMETER, cmd);

    ret = da16k_at_send_formatted_msg(at_message);

    if (ret != DA16K_SUCCESS) {
        DA16K_PRINT("%s: Error sending message: %d\r\n", __func__, (int) ret);
        return ret;
    }
    
    ret = da16k_at_receive_and_validate_response(da16k_at_response_buffer, sizeof(da16k_at_response_buffer), true, expected_response, DA16K_UART_TIMEOUT_MS);

    if (ret != DA16K_SUCCESS && ret != DA16K_AT_ERROR_CODE) {
        return ret;
    }

    if (ret == DA16K_AT_ERROR_CODE) {
        /* We have received an error response, which usually means there are no commands ("ERROR:7"). */

        if (atoi(da16k_at_response_buffer) == -7) {
            DA16K_PRINT("No commands available.\r\n");
            return DA16K_NO_CMDS;
        } else {
            DA16K_PRINT("%s: Bad response: %s\r\n", __func__, da16k_at_response_buffer);
            return DA16K_AT_FAIL;
        }
    }

    /* Now we need to extract the command and parameter (if applicable) */

    ret = DA16K_SUCCESS;

    /* Find space to determine whether we have params or not. We can use strchr here as we're guaranteed a null terminator within the buffer */
    param_ptr = strchr(da16k_at_response_buffer, ' ');

    if (param_ptr != NULL) {
        /* We have params, split the strings */
        *param_ptr = 0x00; /* Null terminate via the space so we can simply use strdup */
        param_ptr++; /* Skip space/null */
        cmd->command = da16k_strdup(da16k_at_response_buffer);
        cmd->parameters = da16k_strdup(param_ptr);

        if (cmd->command == NULL || cmd->parameters == NULL) {
            ret = DA16K_OUT_OF_MEMORY;
        }
    } else {
        /* No parameter, just command */
        cmd->command = da16k_strdup(da16k_at_response_buffer);
        cmd->parameters = NULL;            

        if (cmd->command == NULL) {
            ret = DA16K_OUT_OF_MEMORY;
        }
    }

    return ret;
}

void da16k_destroy_cmd(da16k_cmd_t cmd) {
    if (cmd.command)
        da16k_free(cmd.command);
    if (cmd.parameters)
        da16k_free(cmd.parameters);
}

da16k_err_t da16k_init(const da16k_cfg_t *cfg) {
    /* TODO: do something with cfg... */
    DA16K_RETURN_ON_NULL(DA16K_INVALID_PARAMETER, cfg);

    (void) cfg;

    if (!da16k_uart_init()) {
        return DA16K_UART_ERROR;
    }

    return DA16K_SUCCESS;
}

void da16k_deinit() {
    da16k_uart_close();
}

da16k_msg_t *da16k_create_msg_str(const char *key, const char *value) {
    da16k_msg_t *msg = da16k_malloc(sizeof(da16k_msg_t));

    DA16K_RETURN_ON_NULL(NULL, msg);
    DA16K_RETURN_ON_NULL(NULL, key);
    DA16K_RETURN_ON_NULL(NULL, value);

    msg->key     = da16k_strdup(key);
    msg->value   = da16k_strdup(value);

    if (!msg->key || !msg->value) {
        DA16K_PRINT("DA16K: Memory allocation for key/value failed!");
        da16k_destroy_msg(msg);
        return NULL;
    }

    return msg;
}

da16k_msg_t *da16k_create_msg_float(const char *key, double value) {
/*     platform might not support float printing :( Else we would do:
 *     snprintf(da16k_value_buffer, sizeof(da16k_value_buffer), "%f", value);*/
    if (!da16k_double_to_string(da16k_value_buffer, sizeof(da16k_value_buffer), value)) {
        DA16K_PRINT("%s: Double to string conversion failed!\r\n", __func__);
        return NULL;
    }
    return da16k_create_msg_str(key, da16k_value_buffer);
}

da16k_msg_t *da16k_create_msg_uint(const char *key, uint64_t value) {
    snprintf(da16k_value_buffer, sizeof(da16k_value_buffer), "%" PRIu64, value);
    return da16k_create_msg_str(key, da16k_value_buffer);
}

da16k_msg_t *da16k_create_msg_int(const char *key, int64_t value) {
    snprintf(da16k_value_buffer, sizeof(da16k_value_buffer), "%" PRIi64, value);
    return da16k_create_msg_str(key, da16k_value_buffer);
}

da16k_msg_t *da16k_create_msg_bool(const char *key, bool value) {
    snprintf(da16k_value_buffer, sizeof(da16k_value_buffer), value ? "true" : "false");
    return da16k_create_msg_str(key, da16k_value_buffer);
}

void da16k_destroy_msg(da16k_msg_t *msg) {
    if (msg) {
        da16k_free(msg->key);
        da16k_free(msg->value);
        da16k_free(msg);
    }
}

/* Helper functions for direct sending and destroying (for basic, non-threaded applications) */

/*  Sends a message directly and destroys it immediately after.
    Since it is used from within these wrappers after creation of msg, 
    it returns DA16K_OUT_OF_MEMORY on NULL pointers instead of DA16K_INVALID_PARAMETER as other functions. */
static da16k_err_t da16k_check_send_and_destroy_msg(da16k_msg_t *msg) {
    da16k_err_t ret;

    DA16K_RETURN_ON_NULL(DA16K_OUT_OF_MEMORY, msg);
    DA16K_RETURN_ON_NULL(DA16K_OUT_OF_MEMORY, msg->key);
    DA16K_RETURN_ON_NULL(DA16K_OUT_OF_MEMORY, msg->value);

    ret = da16k_send_msg(msg);
    da16k_destroy_msg(msg);
    return ret;
}

da16k_err_t da16k_send_msg_direct_str(const char *key, const char *value) {
    DA16K_RETURN_ON_NULL(DA16K_INVALID_PARAMETER, key);
    DA16K_RETURN_ON_NULL(DA16K_INVALID_PARAMETER, value);
    return da16k_check_send_and_destroy_msg(da16k_create_msg_str(key, value));
}

da16k_err_t da16k_send_msg_direct_float(const char *key, double value) {
    DA16K_RETURN_ON_NULL(DA16K_INVALID_PARAMETER, key);
    return da16k_check_send_and_destroy_msg(da16k_create_msg_float(key, value));
}

da16k_err_t da16k_send_msg_direct_uint(const char *key, uint64_t value) {
    DA16K_RETURN_ON_NULL(DA16K_INVALID_PARAMETER, key);
    return da16k_check_send_and_destroy_msg(da16k_create_msg_uint(key, value));
}

da16k_err_t da16k_send_msg_direct_int(const char *key, int64_t value) {
    DA16K_RETURN_ON_NULL(DA16K_INVALID_PARAMETER, key);
    return da16k_check_send_and_destroy_msg(da16k_create_msg_int(key, value));
}

da16k_err_t da16k_send_msg_direct_bool(const char *key, bool value) {
    DA16K_RETURN_ON_NULL(DA16K_INVALID_PARAMETER, key);
    return da16k_check_send_and_destroy_msg(da16k_create_msg_bool(key, value));
}

da16k_err_t da16k_send_msg(da16k_msg_t *msg) {
    DA16K_RETURN_ON_NULL(DA16K_INVALID_PARAMETER, msg);
    DA16K_RETURN_ON_NULL(DA16K_INVALID_PARAMETER, msg->key);
    DA16K_RETURN_ON_NULL(DA16K_INVALID_PARAMETER, msg->value);

    /* Expected response: OK | +NWMQMSGSND:1 */
    return da16k_at_send_formatted_and_check_success_code(DA16K_UART_TIMEOUT_MS, "+NWMQMSGSND", "AT+NWICMSG %s,%s", msg->key, msg->value);
}
