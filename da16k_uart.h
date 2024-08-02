/*
 * da16k_uart.h
 *
 *  Created on: Jan 12, 2024
 *      Author: evoirin
 */

#ifndef DA16K_COMM_DA16K_UART_H_
#define DA16K_COMM_DA16K_UART_H_

#include <stdbool.h>
#include "da16k_comm.h"
#include "string.h"

/* Generic uart functionality
 *
 * Link to hardware-specific C-file implementing these functions.
 * These functions MUST be implemented by the application.
 *
 */

#define DA16K_UART_BAUD_RATE        115200
#define DA16K_UART_TIMEOUT_MS       500

bool        da16k_uart_init();
bool        da16k_uart_send(const char *src, size_t length);
da16k_err_t da16k_uart_get_char(char *dst, uint32_t timeout_ms);
void        da16k_uart_close(void);

#endif /* DA16K_COMM_DA16K_UART_H_ */
