/*
 * da16k_comm.c
 *
 *  Created on: Dec 14, 2023
 *      Author: evoirin
 *
 * IoTConnect via Dialog DA16K module.
 * For FreeRTOS.
 *
 * Call da16k_init
 */

#include "da16k_comm.h"

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

#include <string.h>
#include <inttypes.h>

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

#include "da16k_uart.h"

static char                 da16k_value_buffer[64] = {0};
static char                 da16k_send_buffer[256];
static char                 da16k_response_buffer[256];

static void *da16k_malloc(size_t size) {
    return DA16K_CONFIG_MALLOC_FN(size);
}

static void da16k_free(void *ptr) {
    DA16K_CONFIG_FREE_FN(ptr);
}

static char *da16k_strdup(const char* src) {
    size_t str_size = strlen(src) + 1;
    char *ret = da16k_malloc(str_size);

    if (ret) memcpy(ret, src, str_size);

    return ret;
}

static char *da16k_strndup(const char* src, size_t size) {
    size_t str_size = size + 1;
    char *ret = da16k_malloc(str_size);

    if (ret) {
        memcpy(ret, src, str_size);
        ret[str_size] = '\0';
    }

    return ret;
}

static da16k_err_t da16k_receive_full_response(char *buf, size_t buf_size) {
    size_t received_chars = 0;

    /* We basically receive characters until we time out, at which point we've hopefully figured out whether or not we've been answered to */

    memset(buf, 0, buf_size);

    uart_recv(buf, buf_size - 1);
    
    received_chars = strlen(buf);

    if (received_chars == 0) {
        return DA16K_AT_TIMEOUT;
    }

    if (received_chars >= (buf_size-1)) {
        return DA16K_AT_RESPONSE_TOO_LONG;
    }

    return DA16K_SUCCESS;
}

da16k_err_t da16k_get_cmd(da16k_cmd_t *cmd) {
    const char expected_response[] = "\r\n+NWICGETCMD:";
    const char error_response[] = "\r\nERROR:";
    char *expected_response_ptr = NULL;
    char *cmd_ptr = NULL;
    char *cmd_end_ptr = NULL;
    char *error_response_ptr = NULL;
    char *param_ptr = NULL;
    
    da16k_err_t ret = DA16K_SUCCESS;
    ssize_t at_msg_length = snprintf(da16k_send_buffer, sizeof(da16k_send_buffer), "AT+NWICGETCMD\r\n");

    /* DA16K_PRINT("DA16K: Requesting commands. ATCMD: %s", da16k_send_buffer); */

    uart_send(da16k_send_buffer, (size_t) at_msg_length);

    memset(da16k_response_buffer, 0, sizeof(da16k_response_buffer));

    /* Receive the response, length of the expected response, -1 because we don't need to receive a null terminator */
    
    ret = da16k_receive_full_response(da16k_response_buffer, sizeof(da16k_response_buffer));

    if (ret != DA16K_SUCCESS) {
        return ret;
    }

    /* Find either the expected *good* response or the *error* response in the received buffer */

    expected_response_ptr = strstr(da16k_response_buffer, expected_response);
    error_response_ptr = strstr(da16k_response_buffer, error_response);

    if (expected_response_ptr != NULL) {

        /* We have received the GOOD response */

        cmd_ptr = expected_response_ptr + strlen(expected_response);

        if (cmd_ptr >= (da16k_response_buffer + sizeof(da16k_response_buffer))) {
            DA16K_PRINT("ERROR: response cut short\r\n");
            return DA16K_UART_ERROR;
        }

        /* Now we need to extract the command and parameter (if applicable) */

        /* Find end marker \r\n*/

        cmd_end_ptr = strstr(cmd_ptr, "\r\nOK");

        if (cmd_end_ptr == NULL) {
            DA16K_PRINT("ERROR: End marker not found\r\n");
            return DA16K_UART_ERROR;
        }

        /* Find space to determine whether we have params or not */

        param_ptr = memchr(cmd_ptr, ' ',  (size_t) (cmd_end_ptr - cmd_ptr));

        if (param_ptr != NULL) {
            /* We have params, split the strings */

            cmd->command = da16k_strndup(cmd_ptr, (size_t) (param_ptr - cmd_ptr - 1));
            cmd->parameters = da16k_strndup(param_ptr + 1, (size_t) (cmd_end_ptr - param_ptr - 1));
        } else {
            /* No parameter, just command */
            
            cmd->command = da16k_strndup(cmd_ptr, (size_t) (cmd_end_ptr - cmd_ptr - 1));
            cmd->parameters = NULL;            
        }

    } else if (error_response_ptr != NULL) {
        /* We have received an error response, which usually means there are no commands. */

        error_response_ptr += strlen(error_response);

        if (error_response_ptr >= (da16k_response_buffer + sizeof(da16k_response_buffer))) {
            DA16K_PRINT("ERROR: response cut short\r\n");
            return DA16K_UART_ERROR;
        }

        DA16K_PRINT("No commands available.\r\n");

        return DA16K_NO_CMDS;
    } else {
        DA16K_PRINT("Unexpected response: %s\r\n", da16k_response_buffer);
        return DA16K_AT_FAIL;
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

    (void) cfg;

    if (!uart_init(115200, 8, DA16K_UART_PARITY_NONE, 1)) {
        return DA16K_UART_ERROR;
    }

    /* Flush UART */

    uart_recv(da16k_value_buffer, sizeof(da16k_value_buffer));

    return DA16K_SUCCESS;
}

void da16k_deinit() {
    uart_close();
}

da16k_msg_t *da16k_create_msg_str(const char *key, const char *value) {
    da16k_msg_t *msg = da16k_malloc(sizeof(da16k_msg_t));

    assert (key && value);

    if (!msg) {
        DA16K_PRINT("DA16K: Memory allocation for message failed!");
        return NULL;
    }

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
/*     platform might not support float printing :(
 *     snprintf(da16k_value_buffer, sizeof(da16k_value_buffer), "%f", value);*/
    int integer = (int) value;
    int decimal = (int) ((value - (double) integer) * 1000.0f);

    snprintf(da16k_value_buffer, sizeof(da16k_value_buffer), "%d.%03d", integer, abs(decimal));

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

/* Helper functions for direct sending and destroying (for basic, non-threaded applications) */

static da16k_err_t da16k_check_send_and_destroy_msg(da16k_msg_t *msg) {
    da16k_err_t ret;

    if (!msg) {
        return DA16K_OUT_OF_MEMORY;
    }

    if (!msg->key || !msg->value) {
        return DA16K_OUT_OF_MEMORY;
    }

    ret = da16k_send_msg(msg);
    da16k_destroy_msg(msg);
    return ret;
}

da16k_err_t da16k_send_msg_direct_str(const char *key, const char *value) {
    return da16k_check_send_and_destroy_msg(da16k_create_msg_str(key, value));
}

da16k_err_t da16k_send_msg_direct_float(const char *key, double value) {
    return da16k_check_send_and_destroy_msg(da16k_create_msg_float(key, value));
}

da16k_err_t da16k_send_msg_direct_uint(const char *key, uint64_t value) {
    return da16k_check_send_and_destroy_msg(da16k_create_msg_uint(key, value));
}

da16k_err_t da16k_send_msg_direct_int(const char *key, int64_t value) {
    return da16k_check_send_and_destroy_msg(da16k_create_msg_int(key, value));
}

da16k_err_t da16k_send_msg_direct_bool(const char *key, bool value) {
    return da16k_check_send_and_destroy_msg(da16k_create_msg_bool(key, value));
}

da16k_err_t da16k_send_msg(da16k_msg_t *msg) {
    /* Expected response from dialog module is
     *
     * '
     * OK
     *
     * +NWMQMSGSND:1
     * '
     */
    static const char expected_response[] = "\r\nOK\r\n\r\n+NWMQMSGSND:1\r\n";

    da16k_err_t ret = DA16K_SUCCESS;
    ssize_t at_msg_length;
    
    if (!msg->key || !msg->value) {
        DA16K_PRINT("DA16K: Invalid message with empty data!\r\n");
        ret = DA16K_AT_INVALID_MSG;
        goto error;
    }
    
    at_msg_length = snprintf(da16k_send_buffer, sizeof(da16k_send_buffer), "AT+NWICMSG %s,%s\r\n", msg->key, msg->value);

    if (at_msg_length <= 0 || at_msg_length >= (ssize_t) sizeof(da16k_send_buffer)) {
        ret = DA16K_AT_INVALID_MSG;
        goto error;
    }

    DA16K_PRINT("DA16K: Message to DA16K: %s -> %s, ATCMD: %s\r\n", msg->key, msg->value, da16k_send_buffer);

    uart_send(da16k_send_buffer, (size_t) at_msg_length);

    memset(da16k_response_buffer, 0, sizeof(da16k_response_buffer));

    /* Receive the response, length of the expected response, -1 because we don't need to receive a null terminator */
    if (uart_recv(da16k_response_buffer, sizeof(expected_response) - 1)) {
        /* Rudimentary checking in case the buffer is contaminated with other things */
        if (strstr(da16k_response_buffer, "OK") == NULL) {
            ret = DA16K_AT_FAIL;
        }
        if (strstr(da16k_response_buffer, "+NWMQMSGSND:1") == NULL) {
            ret = DA16K_AT_FAIL;
        }
        /* TODO: We could be checking the response a lot nicer here, but it's quite complex to deal with asynchronous data filling the buffer. 
                 A workaround could be simply retrying until we receive the proper response, but this is for the app to decide for now. */
    } else {
        ret = DA16K_AT_TIMEOUT;
    }

    DA16K_PRINT("DA16K: AT Code %d, response '%s'\r\n", ret, da16k_response_buffer);

error:

    return ret;
}

void da16k_destroy_msg(da16k_msg_t *msg) {
    if (msg->key)
        da16k_free(msg->key);
    if (msg->value)
        da16k_free(msg->value);
}
