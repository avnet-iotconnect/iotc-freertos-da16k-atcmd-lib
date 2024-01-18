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

#include <string.h>
#include <inttypes.h>

#include "r_typedefs.h"
#include "console.h"

#include "da16k_uart.h"

typedef struct {
    char *key;
    char *value;
} da16k_msg_t;

#define DA16K_QUEUE_SIZE 32

static QueueHandle_t        da16k_msgQueueHandle                    = NULL;
static StaticQueue_t        da16k_msgQueue                          = {0};
static TaskHandle_t         da16k_thread                            = NULL;
static da16k_msg_t          da16k_queueStorage[DA16K_QUEUE_SIZE]    = {0};
static const uint32_t       da16k_queueWaitTimeMs                   = 500;
static const size_t         da16k_threadStackSize                   = 2048;
static char                 da16k_valueBuffer[64]                   = {0};

static void da16k_commThread(void *pvParameters);

static void da16k_cleanupMsg(da16k_msg_t msg);

da16k_err_t da16k_init(const da16k_cfg_t *cfg) {

    /* TODO: do something with cfg... */

    (void) cfg;

    da16k_msgQueueHandle = xQueueCreateStatic(DA16K_QUEUE_SIZE, sizeof(da16k_msg_t), da16k_queueStorage, &da16k_msgQueue);

    xTaskCreate(da16k_commThread, "DA16K_COMM", da16k_threadStackSize, NULL, 2, &da16k_thread);

    if (NULL == da16k_msgQueueHandle) {
        return DA16K_OUT_OF_MEMORY;
    }

    if (NULL == da16k_thread) {
        return DA16K_OUT_OF_MEMORY;
    }

    if (!uart_init(115200, 8, DA16K_UART_PARITY_NONE, 1)) {
        return DA16K_UART_ERROR;
    }

    /* Flush UART */

    uart_recv(da16k_valueBuffer, sizeof(da16k_valueBuffer));

    return DA16K_SUCCESS;
}

void da16k_deinit() {
    da16k_msg_t msg;

    vTaskDelete(da16k_thread);

    /* Dealloc all remaining messages in queue before deleting the queue itself */

    while (pdPASS == xQueueReceive(da16k_msgQueueHandle, &msg, 0)) {
        da16k_cleanupMsg(msg);
    }

    vQueueDelete(da16k_msgQueueHandle);

    uart_close();
}

static char *da16k_strdup(const char* src) {
    size_t strSize = strlen(src) + 1;
    char *ret = pvPortMalloc(strSize);

    if (ret) memcpy(ret, src, strSize);

    return ret;
}


da16k_err_t da16k_send_str(const char *key, const char *value) {

    da16k_msg_t msg;

    assert (key && value);

    msg.key     = da16k_strdup(key);
    msg.value   = da16k_strdup(value);

    if (!msg.key || !msg.value) {
        da16k_cleanupMsg(msg);
        return DA16K_OUT_OF_MEMORY;
    } else if (pdPASS != xQueueSend(da16k_msgQueueHandle, (void *) &msg, pdMS_TO_TICKS(da16k_queueWaitTimeMs))) {
        da16k_cleanupMsg(msg);
        return DA16K_QUEUE_FULL;
    } else {
        return DA16K_SUCCESS;
    }

}

da16k_err_t da16k_send_float(const char *key, double value) {
/*     platform might not support float printing :(
 *     snprintf(da16k_valueBuffer, sizeof(da16k_valueBuffer), "%f", value);*/
    int integer = (int) value;
    int decimal = (int) ((value - (double) integer) * 1000.0f);

    snprintf(da16k_valueBuffer, sizeof(da16k_valueBuffer), "%d.%03d", integer, abs(decimal));

    return da16k_send_str(key, da16k_valueBuffer);
}

da16k_err_t da16k_send_uint(const char *key, uint64_t value) {
    snprintf(da16k_valueBuffer, sizeof(da16k_valueBuffer), "%" PRIu64, value);
    return da16k_send_str(key, da16k_valueBuffer);
}

da16k_err_t da16k_send_int(const char *key, int64_t value) {
    snprintf(da16k_valueBuffer, sizeof(da16k_valueBuffer), "%" PRIi64, value);
    return da16k_send_str(key, da16k_valueBuffer);
}

da16k_err_t da16k_send_bool(const char *key, bool value) {
    snprintf(da16k_valueBuffer, sizeof(da16k_valueBuffer), value ? "true" : "false");
    return da16k_send_str(key, da16k_valueBuffer);
}


static da16k_err_t da16k_handleMsg(da16k_msg_t msg) {
    static char sendBuffer[128];
    static char responseBuffer[128];

    /* Expected response from dialog module is
     *
     * '
     * OK
     *
     * +NWMQMSGSND1
     * '
     */
    static const char expectedResponse[] = "\r\nOK\r\n\r\n+NWMQMSGSND:1\r\n";

    da16k_err_t ret = DA16K_SUCCESS;
    ssize_t ATMsgLength = snprintf(sendBuffer, sizeof(sendBuffer), "AT+NWICMSG %s,%s\r\n", msg.key, msg.value);

    if (ATMsgLength <= 0 || ATMsgLength >= (ssize_t) sizeof(sendBuffer)) {
        ret = DA16K_AT_INVALID_MSG;
        goto error;
    }

    printf_colour("DA16K: Message to DA16K: %s -> %s, ATCMD: %s\r\n", msg.key, msg.value, sendBuffer);

    uart_send(sendBuffer, (size_t) ATMsgLength);

    memset(responseBuffer, 0, sizeof(responseBuffer));

    /* Receive the response, length of the expected response, -1 because we don't need to receive a null terminator */
    if (uart_recv(responseBuffer, sizeof(expectedResponse) - 1)) {
        if (strstr(responseBuffer, expectedResponse) == NULL) {
            ret = DA16K_AT_FAIL;
        }
    } else {
        ret = DA16K_AT_TIMEOUT;
    }

    printf_colour("DA16K: AT Code %d, response '%s'\r\n", ret, responseBuffer);

error:

    return ret;
}


static void da16k_cleanupMsg(da16k_msg_t msg) {
    if (msg.key)
        vPortFree(msg.key);
    if (msg.value)
        vPortFree(msg.value);
}

void da16k_commThread(void *pvParameters) {
    (void) pvParameters;

    assert(da16k_msgQueueHandle != NULL);

    printf_colour("DA16K: Comm thread running...r\n");

    while (1) {
        da16k_msg_t msg;

        if (pdPASS == xQueueReceive(da16k_msgQueueHandle, &msg, da16k_queueWaitTimeMs)) {
            da16k_handleMsg(msg);
            da16k_cleanupMsg(msg);
        }

        vTaskDelay(1);
    }
}
