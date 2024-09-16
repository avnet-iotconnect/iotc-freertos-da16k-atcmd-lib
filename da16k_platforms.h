/*
 * da16k_platforms.h
 *
 * Config helpers for all supported platforms
 * 
 *  Created on: Jul 19, 2023
 *      Author: evoirin
 */

#ifndef DA16K_COMM_DA16K_PLATFORMS_H_
#define DA16K_COMM_DA16K_PLATFORMS_H_

/* Configuration for RA6Mx platforms

    The configuration must define one of the following:
        DA16K_CONFIG_EK_RA6M4
        DA16K_CONFIG_CK_RA6M5

    The configuration MUST also define the following value:
        DA16K_CONFIG_RENESAS_SCI_UART_CHANNEL

        This value is the UART channel number to use (i.e. which PMOD connector):

        On EK_RA6M4: PMOD1 - 9  | PMOD2 - 0
        On CK_RA6M5: PMOD1 - 9  | PMOD2 - 0
*/

/* Renesas CK-RA6M5 config helper */
#if defined(DA16K_CONFIG_CK_RA6M5)
#include "bsp_api.h"
#include "r_typedefs.h"
#include "console.h"
#undef  DA16K_PRINT
#define DA16K_PRINT                             printf_colour
#define DA16K_CONFIG_RENESAS_SCI_UART
#define DA16K_CONFIG_FREERTOS
#endif

/* Renesas EK-RA6M4 config helper */
#if defined(DA16K_CONFIG_EK_RA6M4)
#include "bsp_api.h"
void ek_ra6m4_printf(const char *format, ...);
#undef  DA16K_PRINT
#define DA16K_PRINT                             ek_ra6m4_printf
#define DA16K_CONFIG_RENESAS_SCI_UART
#define DA16K_CONFIG_FREERTOS
#endif

/* System endianness helper */
# if    (defined(__BIG_ENDIAN__)) || \
        (defined(__BYTE_ORDER__)  && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__) || \
        (defined(__BYTE_ORDER)    && __BYTE_ORDER   == __BIG_ENDIAN) || \
        (defined(_BYTE_ORDER)     && _BYTE_ORDER    == _BIG_ENDIAN) || \
        (defined(BYTE_ORDER)      && BYTE_ORDER     == BIG_ENDIAN)
#       define __ATCMD_BIG_ENDIAN__
# elif  (defined(__LITTLE_ENDIAN__)) || \
        (defined(__BYTE_ORDER__)  && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__) || \
        (defined(__BYTE_ORDER)    && __BYTE_ORDER   == __LITTLE_ENDIAN) || \
        (defined(_BYTE_ORDER)     && _BYTE_ORDER    == _LITTLE_ENDIAN) || \
        (defined(BYTE_ORDER)      && BYTE_ORDER     == LITTLE_ENDIAN)
#       define __ATCMD_LITTLE_ENDIAN__
#endif

#endif /* DA16K_COMM_DA16K_PLATFORMS_H_ */