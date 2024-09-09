#include "da16k_private.h"
#include "da16k_uart.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#define DA16K_AT_TX_BUFFER_SIZE 256
#define DA16K_AT_RX_BUFFER_SIZE 512

static char da16k_at_send_buffer    [DA16K_AT_TX_BUFFER_SIZE];
static char da16k_at_receive_buffer [DA16K_AT_RX_BUFFER_SIZE];
static char da16k_at_saved_response [DA16K_AT_RX_BUFFER_SIZE];

/* Receive a line of AT response from UART with a timeout.

   Returns:
   DA16K_INVALID_PARAMETER - buf or buf_size are invalid.
   DA16K_SUCCESS - a line of text is in the buffer, EXCLUDING \r\n delimiter. 
   DA16K_AT_RESPONSE_TOO_LONG - buf is too small to accommodate the response received.
        The characters for this line will not be flushed in this case, the remainder can be fetched with another call to da16k_at_get_response_line
   DA16K_TIMEOUT - The specified timeout was reached before another character could be fetched. There may be response data in the buffer.

   The function will fetch at most buf_size - 1 characters and the retreived data is guaranteed to be null-terminated, even in case of errors.

   Other errors should not occur in normal operation. */
static da16k_err_t da16k_at_get_response_line(char *buf, size_t buf_size, uint32_t timeout_ms) {
    char       *write_ptr   = buf;
    char       *upper_bound = buf + buf_size - 1;
    char        last_char   = 0x00;
    da16k_err_t ret         = DA16K_SUCCESS;

    DA16K_RETURN_ON_NULL(DA16K_INVALID_PARAMETER, buf);

    if (buf_size == 0) {
        return DA16K_INVALID_PARAMETER;
    }

    memset(buf, 0, buf_size);

    while (true) {
        /* Buffer is full, return & inform */

        if (write_ptr >= upper_bound) {
            return DA16K_AT_RESPONSE_TOO_LONG;
        }

        /* Get next uart char */
        ret = da16k_uart_get_char(write_ptr, timeout_ms);

        if ((last_char == '\r') && (*write_ptr == '\n')) {
            /* We received a full line ended by \r\n */
            write_ptr--;
            write_ptr[0] = 0x00;    /* Replace \r */
            write_ptr[1] = 0x00;    /* Replace \n */
            return DA16K_SUCCESS;
        }

        /* Error */

        if (ret != DA16K_SUCCESS) {
            return ret;
        }

        last_char = *write_ptr;
        write_ptr++;
    }

    return ret;
}

/*  Gets a pointer to the start of AT response data following the colon character. 
    E.g. for ERROR:<x> or +SOMECOMMAND:<x>) it would return a pointer to <x> given the appropriate start_of_response character, if ERROR or +SOMECOMMAND are given in start_of_response.
    Returns NULL if not found or out of bounds.
*/
static char *da16k_at_get_start_of_response_data(char *buf, size_t buf_size, const char *start_of_response) {
    char *ret           = NULL;
    char *upper_bound   = buf + buf_size;

    DA16K_RETURN_ON_NULL(NULL, buf);
    DA16K_RETURN_ON_NULL(NULL, start_of_response);

    ret = strstr(buf, start_of_response);

    if (ret == NULL) {
        return NULL;
    }

    ret += strlen(start_of_response);
    
    /* Final check: colon character & bounds check */
    if (((ret + 1) >= upper_bound) || (*ret != ':')) {
        return NULL;
    }

    return ret + 1;
}

/*  analogous to vprintf, this is like da16k_at_send_formatted_msg but takes va_list as parameter to reduce
    code duplication for other funcs that allow formatted messages to be sent.

    If add_crlf is true, a \r\n will be added at the end.*/
static da16k_err_t da16k_at_send_formatted_valist(bool add_crlf, const char *format, va_list args) {
    int at_msg_length;
    
    DA16K_RETURN_ON_NULL(DA16K_INVALID_PARAMETER, format);

    at_msg_length = vsnprintf(da16k_at_send_buffer, sizeof(da16k_at_send_buffer), format, args);

    if (at_msg_length < 0) {
        return DA16K_AT_INVALID_MSG;
    }

    /* + 2 for \r\n terminator*/
    if ((size_t) (at_msg_length + 2) >= sizeof(da16k_at_send_buffer)) { 
        return DA16K_AT_MESSAGE_TOO_LONG;
    }

    if (add_crlf) {
        /* Add \r\n to terminate the message */
        at_msg_length += sprintf(&da16k_at_send_buffer[at_msg_length], "\r\n");
    }

    DA16K_DEBUG("TX buffer: '%s'", da16k_at_send_buffer);

    return da16k_uart_send(da16k_at_send_buffer, (size_t) at_msg_length) ? DA16K_SUCCESS : DA16K_UART_ERROR;
}

/* checks if any non-space or valid ascii characters are in a response string */
static bool da16k_at_is_line_only_whitespace(const char *str) {
    DA16K_RETURN_ON_NULL(true, str);

    if (strlen(str) == 0) {
        return true;
    }

    while (*str != 0x00) {
        if (*str > ' ') { /* everything above the space character makes the string valid. */
            return false;
        }
        str++;
    }

    return true;
}

da16k_err_t da16k_at_receive_and_validate_response(bool error_possible, const char *expected_response, uint32_t timeout_ms) {
    bool error_received             = false;
    bool ok_received                = false;
    bool response_received          = false;

    static const size_t buf_size    = sizeof(da16k_at_receive_buffer);

    char *upper_bound               = da16k_at_receive_buffer + buf_size;
    char *response_data_start       = NULL;

    da16k_err_t ret                 = DA16K_SUCCESS;
    
    memset(da16k_at_saved_response, 0, buf_size);

    while (ret == DA16K_SUCCESS) {
        ret = da16k_at_get_response_line(da16k_at_receive_buffer, buf_size, timeout_ms);

        if (ret == DA16K_AT_RESPONSE_TOO_LONG) {
            DA16K_WARN("WARNING! RX buffer overflow!\r\nRX Buffer contents:\r\n%s\r\n", da16k_at_receive_buffer);
        }

        /* Ignore lines that don't have anything parseable (just to clean up the output a little) */
        if (da16k_at_is_line_only_whitespace(da16k_at_receive_buffer)) {
            continue;
        }

        DA16K_DEBUG("Respone line received: %s\r\n", da16k_at_receive_buffer);

        /* Look for proper response, if one is expected */
        if (!response_received && expected_response != NULL) {
            response_data_start = da16k_at_get_start_of_response_data(da16k_at_receive_buffer, buf_size, expected_response);
        }

        /* If we can't find the expected response, but an ERROR:<x> is possible and we haven't rcv'd one yet, flag and look for the error response */
        if (error_possible && !error_received && response_data_start == NULL) {
            response_data_start = da16k_at_get_start_of_response_data(da16k_at_receive_buffer, buf_size, "ERROR");
            if (response_data_start != NULL) {
                error_received = true;
            }
        }

        /* Mark whether the OK\r\n part of the response was received. */
        if (strstr(da16k_at_receive_buffer, "OK") != NULL) {
            ok_received = true;
        }

        /* We received a valid response / error response relevant to us */
        if (!response_received && response_data_start != NULL) {
            /* Move all response data to the final response output buffer */
            memmove(da16k_at_saved_response, response_data_start, (size_t) (upper_bound - response_data_start));
            response_received = true;
        }

        /* If we have no expected response, an OK is enough, so we pretend a response was received. */
        if (ok_received && expected_response == NULL) {
            response_received = true;
        }

        /* If OK *or* ERROR and the response data (if any) is received, we break. */
        if (response_received && (error_received || ok_received)) {
            break;
        }
    }

    if (response_received) {
        DA16K_PRINT("da16k_at_saved_response %s\r\n", da16k_at_saved_response);
        if (error_received) {
            ret = DA16K_AT_ERROR_CODE;  /* So caller can handle this case properly */
        } else if (ok_received) {
            ret = DA16K_SUCCESS;        /* Everything is OK */
        } else {
            ret = DA16K_AT_NO_OK;       /* "OK\r\n" was missing */
        }
    }
    
    /* In case of no response when expected, return last error code */
    return ret;
}

da16k_err_t da16k_at_send_formatted_msg(const char *format, ...) {
    va_list args;
    da16k_err_t ret;

    DA16K_RETURN_ON_NULL(DA16K_INVALID_PARAMETER, format);

    va_start(args, format);
    ret = da16k_at_send_formatted_valist(true, format, args);
    va_end(args);

    return ret;
}

da16k_err_t da16k_at_send_formatted_raw_no_crlf(const char *format, ...) {
    va_list args;
    da16k_err_t ret;

    DA16K_RETURN_ON_NULL(DA16K_INVALID_PARAMETER, format);

    va_start(args, format);
    ret = da16k_at_send_formatted_valist(false, format, args);
    va_end(args);

    return ret;
}

da16k_err_t da16k_at_send_formatted_and_check_success(uint32_t timeout_ms, const char *expected_response, const char *format, ...) {
    da16k_err_t ret = DA16K_SUCCESS;
    va_list fmt_args;

    DA16K_RETURN_ON_NULL(DA16K_INVALID_PARAMETER, format);

    va_start(fmt_args, format);
    ret = da16k_at_send_formatted_valist(true, format, fmt_args);
    va_end(fmt_args);

    if (ret != DA16K_SUCCESS) {
        DA16K_ERROR("Error sending message: %d\r\n", (int) ret);
        return ret;
    }

    ret = da16k_at_receive_and_validate_response(false, expected_response, timeout_ms);

    /* Only check the return code if we have an expected response. */

    if (expected_response) {
        if (ret == DA16K_SUCCESS && da16k_at_get_response_code() != 1) {
            DA16K_ERROR("AT command not successful. Return code: %d\r\n", da16k_at_get_response_code());
            ret = DA16K_AT_FAIL;
        }
    }

    return ret;
}

da16k_err_t da16k_at_send_certificate(da16k_cert_type_t type, const char *cert) {
    char        command_sequence[]  = AT_ESC "C0,";
    bool        tx_success          = true;

    DA16K_RETURN_ON_NULL(DA16K_INVALID_PARAMETER, cert);

    /* A bit hackish, but the number after the 'C' denotes the certificate type, so we adjust it. */
    command_sequence[2] += (char) type;

    tx_success &= da16k_uart_send(command_sequence, strlen(command_sequence));  /* Enter Certificate Command Mode */
    tx_success &= da16k_uart_send(cert,             strlen(cert));              /* Actual certificate */
    tx_success &= da16k_uart_send(AT_ETX,           1);                         /* End of text marker */

    if (!tx_success) {
        return DA16K_UART_ERROR;
    }

    return da16k_at_receive_and_validate_response(false, NULL, DA16K_UART_TIMEOUT_MS);
}

char *da16k_at_get_response_str(void) {
    return da16k_strdup(da16k_at_saved_response);
}

int da16k_at_get_response_code(void) {
    /* TODO: Make this less error-prone */
    return atoi(da16k_at_saved_response);
}
