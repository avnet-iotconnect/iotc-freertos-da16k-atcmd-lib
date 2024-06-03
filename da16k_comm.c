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

typedef struct {
    char *key;
    char *value;
} da16k_msg_t;


FSP_HEADER

#define DA16K_QUEUE_SIZE 32

static QueueHandle_t        da16k_msg_queue_handle                  = NULL;
static StaticQueue_t        da16k_msg_queue                         = {0};
static TaskHandle_t         da16k_thread                            = NULL;
static da16k_msg_t          da16k_queue_storage[DA16K_QUEUE_SIZE]   = {0};
static const uint32_t       da16k_queue_wait_time_ms                = 500;
static const size_t         da16k_thread_stack_size                 = 2048;
static char                 da16k_value_buffer[64]                  = {0};
static char                 da16k_send_buffer[256];
static char                 da16k_response_buffer[256];

static void da16k_comm_thread(void *pvParameters);
static void da16k_destroy_msg(da16k_msg_t msg);

static char *da16k_strdup(const char* src) {
    size_t strSize = strlen(src) + 1;
    char *ret = pvPortMalloc(strSize);

    if (ret) memcpy(ret, src, strSize);

    return ret;
}

static char *da16k_strndup(const char* src, size_t size) {
    size_t strSize = size + 1;
    char *ret = pvPortMalloc(strSize);

    if (ret) {
        memcpy(ret, src, strSize);
        ret[strSize] = '\0';
    }

    return ret;
}

static da16k_err_t da16k_receive_command_response(char *buf, size_t buf_size) {
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

da16k_err_t da16k_get_cmd(da16k_cmd_t *cmdToReceive) {
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
    
    ret = da16k_receive_command_response(da16k_response_buffer, sizeof(da16k_response_buffer));

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

            cmdToReceive->command = da16k_strndup(cmd_ptr, (size_t) (param_ptr - cmd_ptr - 1));
            cmdToReceive->parameters = da16k_strndup(param_ptr + 1, (size_t) (cmd_end_ptr - param_ptr - 1));
        } else {
            /* No parameter, just command */
            
            cmdToReceive->command = da16k_strndup(cmd_ptr, (size_t) (cmd_end_ptr - cmd_ptr - 1));
            cmdToReceive->parameters = NULL;            
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
        vPortFree(cmd.command);
    if (cmd.parameters)
        vPortFree(cmd.parameters);
}

da16k_err_t da16k_init(const da16k_cfg_t *cfg) {

    /* TODO: do something with cfg... */

    (void) cfg;

    da16k_msg_queue_handle = xQueueCreateStatic(DA16K_QUEUE_SIZE, sizeof(da16k_msg_t), (uint8_t *) da16k_queue_storage, &da16k_msg_queue);

    xTaskCreate(da16k_comm_thread, "DA16K_COMM", da16k_thread_stack_size, NULL, 2, &da16k_thread);

    if (NULL == da16k_msg_queue_handle) {
        return DA16K_OUT_OF_MEMORY;
    }

    if (NULL == da16k_thread) {
        return DA16K_OUT_OF_MEMORY;
    }

    if (!uart_init(115200, 8, DA16K_UART_PARITY_NONE, 1)) {
        return DA16K_UART_ERROR;
    }

    /* Flush UART */

    uart_recv(da16k_value_buffer, sizeof(da16k_value_buffer));

    return DA16K_SUCCESS;
}

void da16k_deinit() {
    da16k_msg_t msg;

    vTaskDelete(da16k_thread);

    /* Dealloc all remaining messages in queue before deleting the queue itself */

    while (pdPASS == xQueueReceive(da16k_msg_queue_handle, &msg, 0)) {
        da16k_destroy_msg(msg);
    }

    vQueueDelete(da16k_msg_queue_handle);

    uart_close();
}

da16k_err_t da16k_send_str(const char *key, const char *value) {

    da16k_msg_t msg;

    assert (key && value);

    msg.key     = da16k_strdup(key);
    msg.value   = da16k_strdup(value);

    if (!msg.key || !msg.value) {
        da16k_destroy_msg(msg);
        return DA16K_OUT_OF_MEMORY;
    } else if (pdPASS != xQueueSend(da16k_msg_queue_handle, (void *) &msg, pdMS_TO_TICKS(da16k_queue_wait_time_ms))) {
        da16k_destroy_msg(msg);
        return DA16K_QUEUE_FULL;
    } else {
        return DA16K_SUCCESS;
    }

}

da16k_err_t da16k_send_float(const char *key, double value) {
/*     platform might not support float printing :(
 *     snprintf(da16k_value_buffer, sizeof(da16k_value_buffer), "%f", value);*/
    int integer = (int) value;
    int decimal = (int) ((value - (double) integer) * 1000.0f);

    snprintf(da16k_value_buffer, sizeof(da16k_value_buffer), "%d.%03d", integer, abs(decimal));

    return da16k_send_str(key, da16k_value_buffer);
}

da16k_err_t da16k_send_uint(const char *key, uint64_t value) {
    snprintf(da16k_value_buffer, sizeof(da16k_value_buffer), "%" PRIu64, value);
    return da16k_send_str(key, da16k_value_buffer);
}

da16k_err_t da16k_send_int(const char *key, int64_t value) {
    snprintf(da16k_value_buffer, sizeof(da16k_value_buffer), "%" PRIi64, value);
    return da16k_send_str(key, da16k_value_buffer);
}

da16k_err_t da16k_send_bool(const char *key, bool value) {
    snprintf(da16k_value_buffer, sizeof(da16k_value_buffer), value ? "true" : "false");
    return da16k_send_str(key, da16k_value_buffer);
}


static da16k_err_t da16k_handleMsg(da16k_msg_t msg) {
    /* Expected response from dialog module is
     *
     * '
     * OK
     *
     * +NWMQMSGSND1
     * '
     */
    static const char expected_response[] = "\r\nOK\r\n\r\n+NWMQMSGSND:1\r\n";

    da16k_err_t ret = DA16K_SUCCESS;
    ssize_t at_msg_length = snprintf(da16k_send_buffer, sizeof(da16k_send_buffer), "AT+NWICMSG %s,%s\r\n", msg.key, msg.value);

    if (at_msg_length <= 0 || at_msg_length >= (ssize_t) sizeof(da16k_send_buffer)) {
        ret = DA16K_AT_INVALID_MSG;
        goto error;
    }

    DA16K_PRINT("DA16K: Message to DA16K: %s -> %s, ATCMD: %s\r\n", msg.key, msg.value, da16k_send_buffer);

    uart_send(da16k_send_buffer, (size_t) at_msg_length);

    memset(da16k_response_buffer, 0, sizeof(da16k_response_buffer));

    /* Receive the response, length of the expected response, -1 because we don't need to receive a null terminator */
    if (uart_recv(da16k_response_buffer, sizeof(expected_response) - 1)) {
        if (strstr(da16k_response_buffer, expected_response) == NULL) {
            ret = DA16K_AT_FAIL;
        }
    } else {
        ret = DA16K_AT_TIMEOUT;
    }

    DA16K_PRINT("DA16K: AT Code %d, response '%s'\r\n", ret, da16k_response_buffer);

error:

    return ret;
}


static void da16k_destroy_msg(da16k_msg_t msg) {
    if (msg.key)
        vPortFree(msg.key);
    if (msg.value)
        vPortFree(msg.value);
}

void da16k_comm_thread(void *pvParameters) {
    (void) pvParameters;

    assert(da16k_msg_queue_handle != NULL);

    DA16K_PRINT("DA16K: Comm thread running...r\n");

    while (1) {
        da16k_msg_t msg;

        if (pdPASS == xQueueReceive(da16k_msg_queue_handle, &msg, da16k_queue_wait_time_ms)) {
            da16k_handleMsg(msg);
            da16k_destroy_msg(msg);
        }

        vTaskDelay(1);
    }
}
