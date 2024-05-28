/*
 * Example config for the da16k AT command library
 */

#ifndef DA16K_COMM_DA16K_CONFIG_H_
#define DA16K_COMM_DA16K_CONFIG_H_

/*
 * Supported Targets
 */

/* Enable Renesas EK-RA6M4 Eval Kit Target */
#define DA16K_CONFIG_EK_RA6M4

/* Enable Renesas CK-RA6M5 Cloud Kit Target
#define DA16K_CONFIG_CK_RA6M5
*/

/*
 * Target-specific option
 */

/* Renesas UART channel for CK-RA6M5/EK-RA6M4: PMOD1 = 9, PMOD2 = 0
 *
 * this MUST reflect your project configuration! */
#define DA16K_CONFIG_RENESAS_SCI_UART_CHANNEL   9

#endif /* DA16K_COMM_DA16K_CONFIG_H_ */
