/*
 * uart_ra6m5.c
 *
 *  Created on: Jan 12, 2024
 *      Author: evoirin
 */

#include "da16k_config.h"

#if defined(DA16K_CONFIG_RENESAS_SCI_UART)

#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "r_sci_uart.h"
#include "r_uart_api.h"

#include "da16k_uart.h"

#include "FreeRTOS.h"
#include "task.h"

#define _SCI_VECTOR(channel, interrupt) VECTOR_NUMBER_SCI ## channel ## _ ## interrupt
#define SCI_VECTOR(channel, interrupt) _SCI_VECTOR(channel, interrupt)

#define RA6M5_UART_CHANNEL_NUM      DA16K_CONFIG_RENESAS_SCI_UART_CHANNEL

#define RA6M5_UART_TIMEOUT_MS       DA16K_UART_TIMEOUT_MS

static sci_uart_instance_ctrl_t ra6_uart_ctrl = {0};

static baud_setting_t ra6_uart_baud_setting = {0};

/** UART extended configuration for UARTonSCI HAL driver */
static sci_uart_extended_cfg_t ra6_uart_cfg_extend = {
                                                    .clock = SCI_UART_CLOCK_INT,
                                                    .rx_edge_start = SCI_UART_START_BIT_FALLING_EDGE,
                                                    .noise_cancel = SCI_UART_NOISE_CANCELLATION_DISABLE,
                                                    .rx_fifo_trigger = SCI_UART_RX_FIFO_TRIGGER_MAX,
                                                    .p_baud_setting = &ra6_uart_baud_setting,
                                                    .flow_control = SCI_UART_FLOW_CONTROL_RTS,
                                                    .flow_control_pin = (bsp_io_port_pin_t) UINT16_MAX,
                                                    .rs485_setting =  {
                                                                        .enable = SCI_UART_RS485_DISABLE,
                                                                        .polarity = SCI_UART_RS485_DE_POLARITY_HIGH,
                                                                        .de_control_pin = (bsp_io_port_pin_t) UINT16_MAX,
                                                    },
};

static bool     g_tx_complete = false;
static bool     g_rx_complete = false;

static void ra6m5_uart_callback (uart_callback_args_t * p_args)
{
    /* Handle the UART event */
    switch (p_args->event)
    {
        case UART_EVENT_RX_COMPLETE:    g_rx_complete = true; break;
        case UART_EVENT_TX_COMPLETE:    g_tx_complete = true; break;
        default:                        return;
    }
}

/** UART interface configuration */
static uart_cfg_t ra6_uart_cfg = {
                                .channel = RA6M5_UART_CHANNEL_NUM,
                                .data_bits = UART_DATA_BITS_8,
                                .parity = UART_PARITY_OFF,
                                .stop_bits = UART_STOP_BITS_1,
                                .p_callback = ra6m5_uart_callback,
                                .p_context = NULL,
                                .p_extend = &ra6_uart_cfg_extend,
                                .p_transfer_tx = NULL,
                                .p_transfer_rx = NULL,
                                .rxi_ipl = (12),
                                .txi_ipl = (12),
                                .tei_ipl = (12),
                                .eri_ipl = (12),
                                .rxi_irq = SCI_VECTOR(RA6M5_UART_CHANNEL_NUM, RXI),
                                .txi_irq = SCI_VECTOR(RA6M5_UART_CHANNEL_NUM, TXI),
                                .tei_irq = SCI_VECTOR(RA6M5_UART_CHANNEL_NUM, TEI),
                                .eri_irq = SCI_VECTOR(RA6M5_UART_CHANNEL_NUM, ERI),
};

bool uart_init(uint32_t baud, uint32_t bits, uint32_t parity, uint32_t stopbits) {

    fsp_err_t ret = FSP_SUCCESS;

    ret = R_SCI_UART_BaudCalculate(baud, false, 5*1000, &ra6_uart_baud_setting);

    if (ret != FSP_SUCCESS)
        return false;

    switch (bits) {
        case 7:     ra6_uart_cfg.data_bits = UART_DATA_BITS_7; break;
        case 8:     ra6_uart_cfg.data_bits = UART_DATA_BITS_8; break;
        case 9:     ra6_uart_cfg.data_bits = UART_DATA_BITS_9; break;
        default:    return false;
    };

    switch (parity) {
        case DA16K_UART_PARITY_NONE:    ra6_uart_cfg.parity = UART_PARITY_OFF; break;
        case DA16K_UART_PARITY_ODD:     ra6_uart_cfg.parity = UART_PARITY_ODD; break;
        case DA16K_UART_PARITY_EVEN:    ra6_uart_cfg.parity = UART_PARITY_EVEN; break;
        default:                        return false;
    };

    switch (stopbits) {
        case 1:     ra6_uart_cfg.stop_bits = UART_STOP_BITS_1; break;
        case 2:     ra6_uart_cfg.stop_bits = UART_STOP_BITS_2; break;
        default:    return false;
    }

    ret = R_SCI_UART_Open(&ra6_uart_ctrl, &ra6_uart_cfg);

    if (ret != FSP_SUCCESS) {
        return false;
    }

    ret = R_SCI_UART_CallbackSet(&ra6_uart_ctrl, ra6m5_uart_callback, NULL, NULL);

    return (ret == FSP_SUCCESS);
}

bool uart_send(const char *src, size_t length) {
    fsp_err_t err;

    g_tx_complete = false;

    err = R_SCI_UART_Write(&ra6_uart_ctrl, (const uint8_t *) src, length);

    while (err == FSP_SUCCESS && !g_tx_complete) { taskYIELD(); }

    return (err == FSP_SUCCESS);
}

bool uart_recv(char *dst, size_t length) {
    TickType_t ticksTimeout = xTaskGetTickCount() + pdMS_TO_TICKS(RA6M5_UART_TIMEOUT_MS);

    bool ok = (FSP_SUCCESS == R_SCI_UART_Read(&ra6_uart_ctrl, (uint8_t *) dst, length));

    g_rx_complete = false;

    while (ok && !g_rx_complete) {
        taskYIELD();
        ok = (xTaskGetTickCount() < ticksTimeout);
    }

    return ok;
}

bool uart_close() {
    fsp_err_t err = R_SCI_UART_Close(&ra6_uart_ctrl);

    return (err == FSP_SUCCESS);
}

#endif
