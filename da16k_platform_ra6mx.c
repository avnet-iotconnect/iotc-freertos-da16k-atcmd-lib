/*
 * IoTConnect DA16K AT Command Library
 * Platform functions implementation for RA6Mx platforms
 *
 *  Created on: Jan 12, 2024
 *      Author: evoirin
 */

#include "da16k_comm/da16k_comm.h"

#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "r_sci_uart.h"
#include "r_uart_api.h"

#include "da16k_comm/da16k_uart.h"

#include "FreeRTOS.h"
#include "queue.h"
#include "task.h"

#define _SCI_VECTOR(channel, interrupt) VECTOR_NUMBER_SCI ## channel ## _ ## interrupt
#define SCI_VECTOR(channel, interrupt) _SCI_VECTOR(channel, interrupt)

#define RA6MX_UART_CHANNEL_NUM      DA16K_CONFIG_RENESAS_SCI_UART_CHANNEL

#define RA6MX_UART_TIMEOUT_MS       DA16K_UART_TIMEOUT_MS

#define RA6MX_UART_RX_FIFO_SIZE     1024

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

static bool             g_tx_complete = false;
static QueueHandle_t    g_rx_char_fifo;

static void ra6mx_uart_callback (uart_callback_args_t * p_args)
{
    char received_char;

    if (p_args == NULL) {
        /* something went *very* wrong */
        return;
    }

    /* Handle the UART event */
    switch (p_args->event)
    {
        case UART_EVENT_RX_CHAR:
        {
            received_char = (char) p_args->data;
            xQueueSendToBackFromISR(g_rx_char_fifo, &received_char, NULL);
            break;
        }
        case UART_EVENT_TX_COMPLETE:
        {
            g_tx_complete = true;
            break;
        }
        default:
        {
            return;
        }
    }
}

/** UART interface configuration */
static uart_cfg_t ra6_uart_cfg = {
                                .channel = RA6MX_UART_CHANNEL_NUM,
                                .data_bits = UART_DATA_BITS_8,
                                .parity = UART_PARITY_OFF,
                                .stop_bits = UART_STOP_BITS_1,
                                .p_callback = ra6mx_uart_callback,
                                .p_context = NULL,
                                .p_extend = &ra6_uart_cfg_extend,
                                .p_transfer_tx = NULL,
                                .p_transfer_rx = NULL,
                                .rxi_ipl = (12),
                                .txi_ipl = (12),
                                .tei_ipl = (12),
                                .eri_ipl = (12),
                                .rxi_irq = SCI_VECTOR(RA6MX_UART_CHANNEL_NUM, RXI),
                                .txi_irq = SCI_VECTOR(RA6MX_UART_CHANNEL_NUM, TXI),
                                .tei_irq = SCI_VECTOR(RA6MX_UART_CHANNEL_NUM, TEI),
                                .eri_irq = SCI_VECTOR(RA6MX_UART_CHANNEL_NUM, ERI),
};

bool da16k_uart_init(void) {

    fsp_err_t ret = FSP_SUCCESS;

    /* implement char FIFO using a FreeRTOS queue */

    g_rx_char_fifo = xQueueCreate(RA6MX_UART_RX_FIFO_SIZE, sizeof(char));

    if (g_rx_char_fifo == NULL) {
        return false;
    }

    /* configure & open UART */

    ret = R_SCI_UART_BaudCalculate(DA16K_UART_BAUD_RATE, false, 5*1000, &ra6_uart_baud_setting);

    if (ret != FSP_SUCCESS)
        return false;

    ret = R_SCI_UART_Open(&ra6_uart_ctrl, &ra6_uart_cfg);

    if (ret != FSP_SUCCESS) {
        return false;
    }

    ret = R_SCI_UART_CallbackSet(&ra6_uart_ctrl, ra6mx_uart_callback, NULL, NULL);

    return (ret == FSP_SUCCESS);
}

bool da16k_uart_send(const char *src, size_t length) {
    fsp_err_t err;

    g_tx_complete = false;

    err = R_SCI_UART_Write(&ra6_uart_ctrl, (const uint8_t *) src, length);

    while (err == FSP_SUCCESS && !g_tx_complete) { taskYIELD(); }

    return (err == FSP_SUCCESS);
}

da16k_err_t da16k_uart_get_char(char *dst, uint32_t timeout_ms) {

    BaseType_t ret;

    if (g_rx_char_fifo == NULL) {
        return DA16K_NOT_INITIALIZED;
    }

    ret = xQueueReceive(g_rx_char_fifo, dst, pdMS_TO_TICKS(timeout_ms));

    if (ret == pdFALSE) {
        return DA16K_TIMEOUT;
    }

    return DA16K_SUCCESS;
}

void da16k_uart_close(void) {
    R_SCI_UART_Close(&ra6_uart_ctrl);
}

#if defined(DA16K_CONFIG_EK_RA6M4)
#include "usb_console_main.h"
#define EK_RA6M4_PRINTF_BUFFER_SIZE 512

/*
 * The USB console on the EK RA6M4 platform lacks a formatted print function, so we implement one ourselves.
 */

void ek_ra6m4_printf(const char *format, ...) {
    char tmp_buf[EK_RA6M4_PRINTF_BUFFER_SIZE] = { 0, };
    va_list args;
    va_start(args, format);
    vsnprintf(tmp_buf, EK_RA6M4_PRINTF_BUFFER_SIZE, format, args);
    va_end(args);
    print_to_console(tmp_buf);
}

#endif
