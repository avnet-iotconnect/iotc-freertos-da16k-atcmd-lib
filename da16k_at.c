#include "da16k_private.h"

static char da16k_at_send_buffer[256];
static char da16k_at_response_buffer[512];

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
    code duplication for other funcs that allow formatted messages to be sent */
static da16k_err_t da16k_at_send_formatted_valist(const char *format, va_list args) {
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

    /* Add \r\n to terminate the message */
    at_msg_length += sprintf(&da16k_at_send_buffer[at_msg_length], "\r\n");

    DA16K_PRINT("%s -> %s", __func__, da16k_at_send_buffer);

    return da16k_uart_send(da16k_at_send_buffer, (size_t) at_msg_length) ? DA16K_SUCCESS : DA16K_UART_ERROR;
}

da16k_err_t da16k_at_receive_and_validate_response(bool error_possible, const char *expected_response, uint32_t timeout_ms) {
    bool error_received         = false;
    bool ok_received            = false;

    static const buf_size = sizeof(da16k_at_response_buffer);

    char *upper_bound           = da16k_at_response_buffer + buf_size;
    char *response_data_start   = NULL;

    da16k_err_t ret             = DA16K_SUCCESS;
    
    while (ret == DA16K_SUCCESS) {
        ret = da16k_at_get_response_line(da16k_at_response_buffer, buf_size, timeout_ms);

        if (ret == DA16K_AT_RESPONSE_TOO_LONG) {
            DA16K_PRINT("%s: WARNING! RX buffer overflow!\r\nRX Buffer contents:\r\n%s\r\n", __func__, buf);
        }


        /* Look for proper response */
        response_data_start = da16k_at_get_start_of_response_data(da16k_at_response_buffer, buf_size, expected_response);

        /* If we can't find the expected response, but an ERROR:<x> is possible, flag and look for the error response */
        if (error_possible && (response_data_start == NULL)) {
            response_data_start = da16k_at_get_start_of_response_data(da16k_at_response_buffer, buf_size, "ERROR");
            if (response_data_start != NULL) {
                error_received = true;
            }
        }

        /* Mark whether the OK\r\n part of the response was received. */
        if (strstr(da16k_at_response_buffer, "OK") != NULL) {
            ok_received = true;
        }

        /* We received a valid response relevant to us, break */
        if (response_data_start) {
            break;
        }
    }

    if (response_data_start) {
        /* Move all response data to the start of the buffer; memmove means we don't need an intermediate buffer */
        memmove(da16k_at_response_buffer, response_data_start, (size_t) (upper_bound - response_data_start));
        
        if (error_received) {
            ret = DA16K_AT_ERROR_CODE;  /* So caller can handle this case properly */
        } else if (ok_received) {
            ret = DA16K_SUCCESS;        /* Everything is OK */
        } else {
            ret = DA16K_AT_NO_OK;       /* "OK\r\n" was missing */
        }
    }
    
    /* In case of no response data, return last error code */
    return ret;
}


da16k_err_t da16k_at_send_formatted_msg(const char *format, ...) {
    va_list args;
    da16k_err_t ret;

    DA16K_RETURN_ON_NULL(DA16K_INVALID_PARAMETER, format);

    va_start(args, format);
    ret = da16k_at_send_formatted_valist(format, args);
    va_end(args);

    return ret;
 }

da16k_err_t da16k_at_send_formatted_and_check_success_code(uint32_t timeout_ms, const char *expected_response, const char *format, ...) {
    da16k_err_t ret = DA16K_SUCCESS;
    va_list fmt_args;

    DA16K_RETURN_ON_NULL(DA16K_INVALID_PARAMETER, expected_response);
    DA16K_RETURN_ON_NULL(DA16K_INVALID_PARAMETER, format);

    va_start(fmt_args, format);
    ret = da16k_at_send_formatted_valist(format, fmt_args);
    va_end(fmt_args);

    if (ret != DA16K_SUCCESS) {
        DA16K_PRINT("%s: Error sending message: %d\r\n", __func__, (int) ret);
        return ret;
    }

    ret = da16k_at_receive_and_validate_response(false, expected_response, timeout_ms);

    if (ret == DA16K_SUCCESS && da16k_at_get_response_code() != 1) {
        DA16K_PRINT("%s: AT command not successful. Return code: %d\r\n", __func__, da16k_at_get_response_code());
        ret = DA16K_AT_FAIL;
    }

    return ret;
}

char *da16k_at_get_response_str(void) {
    return da16k_strdup(da16k_at_response_buffer);
}

int da16k_at_get_response_code(void) {
    /* TODO: Make this less error-prone */
    return atoi(da16k_at_response_buffer);
}