/*
 * da16k_uart.h
 *
 *  Created on: Jan 12, 2024
 *      Author: evoirin
 */

#ifndef DA16K_COMM_DA16K_UART_H_
#define DA16K_COMM_DA16K_UART_H_

/* Generic uart functionality
 *
 * Link to hardware-specific C-file implementing these functions.
 * These functions MUST be implemented by the application.
 *
 */

#define DA16K_UART_PARITY_NONE      0
#define DA16K_UART_PARITY_ODD       1
#define DA16K_UART_PARITY_EVEN      2

#define DA16K_UART_TIMEOUT_MS       500

bool uart_init(uint32_t baud, uint32_t bits, uint32_t parity, uint32_t stopbits);
bool uart_send(const char *src, size_t length);
bool uart_recv(char *dst, size_t length);
bool uart_close();

#endif /* DA16K_COMM_DA16K_UART_H_ */
