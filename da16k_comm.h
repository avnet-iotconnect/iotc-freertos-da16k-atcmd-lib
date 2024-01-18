/*
 * da16k_comm.h
 *
 * IoTConnect communication via DIALOG DA16600 PMOD wifi module
 *
 *  Created on: Dec 14, 2023
 *      Author: evoirin
 */

#ifndef DA16K_COMM_DA16K_COMM_H_
#define DA16K_COMM_DA16K_COMM_H_

#include "bsp_api.h"
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "hal_data.h"

typedef struct {
    /* Nothing configurable yet */
} da16k_cfg_t;

typedef enum e_da16k_err {
    DA16K_SUCCESS           = 0,
    DA16K_OUT_OF_MEMORY     = 1,
    DA16K_UART_ERROR        = 2,
    DA16K_AT_TIMEOUT        = 3,
    DA16K_AT_FAIL           = 4,
    DA16K_AT_INVALID_MSG    = 5,
    DA16K_QUEUE_FULL        = 6,
} da16k_err_t;

da16k_err_t da16k_init(const da16k_cfg_t *cfg);
void        da16k_deinit();
da16k_err_t da16k_send_str(const char* key, const char* value);
da16k_err_t da16k_send_float(const char *key, double value);
da16k_err_t da16k_send_uint(const char *key, uint64_t value);
da16k_err_t da16k_send_int(const char *key, int64_t value);
da16k_err_t da16k_send_bool(const char *key, bool value);

#endif /* DA16K_COMM_DA16K_COMM_H_ */
