/*
 * da16k_config.h
 *
 *  Created on: Jan 23, 2024
 *      Author: evoirin
 */

#ifndef DA16K_COMM_DA16K_CONFIG_H_
#define DA16K_COMM_DA16K_CONFIG_H_

/* Enable Renesas CK-RA6M5 Cloud Kit Target */
#define DA16K_CONFIG_CK_RA6M5   1

/* Enable generic printf */
#define DA16K_PRINT                             printf

/* Renesas CK-RA6M5 config */

#if defined(DA16K_CONFIG_CK_RA6M5)
#include "bsp_api.h"
#include "r_typedefs.h"
#include "console.h"
#undef  DA16K_PRINT
#define DA16K_PRINT                             printf_colour
#define DA16K_CONFIG_RENESAS_SCI_UART           1

/* Renesas UART channel for CK-RA6M5: PMOD1 = 9, PMOD2 = 0
 *
 * this MUST reflect your project configuration! */
#define DA16K_CONFIG_RENESAS_SCI_UART_CHANNEL   9
#endif

#endif /* DA16K_COMM_DA16K_CONFIG_H_ */
