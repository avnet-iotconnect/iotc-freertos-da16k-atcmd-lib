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

#include <stdint.h>
#include <stdbool.h>

#if defined(DA16K_CONFIG_FILE)
#include DA16K_CONFIG_FILE
#else
#error "Please define DA16K_CONFIG_FILE!"
#endif

/* Enable generic printf */
#define DA16K_PRINT             printf

/* Renesas CK-RA6M5 config */

#if defined(DA16K_CONFIG_CK_RA6M5)
#include "bsp_api.h"
#include "r_typedefs.h"
#include "console.h"
#undef  DA16K_PRINT
#define DA16K_PRINT                             printf_colour
#define DA16K_CONFIG_RENESAS_SCI_UART
#endif


#if defined(DA16K_CONFIG_EK_RA6M4)
#include "bsp_api.h"
void ek_ra6m4_printf(const char *format, ...);
#undef  DA16K_PRINT
#define DA16K_PRINT                             ek_ra6m4_printf
#define DA16K_CONFIG_RENESAS_SCI_UART
#endif


typedef struct {
    /* TODO FIXME:
        Nothing configurable yet */
} da16k_cfg_t;

typedef enum e_da16k_err {
    DA16K_SUCCESS               = 0,
    DA16K_OUT_OF_MEMORY         = 1,
    DA16K_UART_ERROR            = 2,
    DA16K_AT_TIMEOUT            = 3,
    DA16K_AT_FAIL               = 4,
    DA16K_AT_INVALID_MSG        = 5,
    DA16K_AT_RESPONSE_TOO_LONG  = 6,
    DA16K_QUEUE_FULL            = 7,
    DA16K_NO_CMDS               = 8,
} da16k_err_t;

typedef struct {
    char *command;
    char *parameters;
} da16k_cmd_t;

da16k_err_t da16k_init(const da16k_cfg_t *cfg);
void        da16k_deinit();
da16k_err_t da16k_send_str(const char* key, const char* value);
da16k_err_t da16k_send_float(const char *key, double value);
da16k_err_t da16k_send_uint(const char *key, uint64_t value);
da16k_err_t da16k_send_int(const char *key, int64_t value);
da16k_err_t da16k_send_bool(const char *key, bool value);

/* Receives the next command from the AT command gateway. The strings here must be free'd after use. */
da16k_err_t da16k_getCmd(da16k_cmd_t *cmdToReceive);

#endif /* DA16K_COMM_DA16K_COMM_H_ */
