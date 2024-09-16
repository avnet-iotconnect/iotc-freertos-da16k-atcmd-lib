/*
 * da16k_comm.c
 *
 *  Created on: Dec 14, 2023
 *      Author: evoirin
 *
 * IoTConnect via Dialog DA16K module.
 */

#include "da16k_comm.h"
#include "da16k_private.h"

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdarg.h>

#include <string.h>
#include <inttypes.h>
#include <assert.h>

#include "da16k_uart.h"

#pragma GCC diagnostic error "-Wextra"

typedef enum e_da16k_msg_data_type {
    DA16K_AT_STRING = 0,
    DA16K_AT_BOOL,
    DA16K_AT_FLOAT32,
    DA16K_AT_FLOAT64
} da16k_msg_data_type_t;

typedef union {
    char       *d_string;
    bool        d_bool;
    float       d_float32;
    double      d_float64;
} da16k_value_t;

typedef struct {
    da16k_msg_data_type_t   type;
    const char             *key;
    da16k_value_t           value;
} da16k_msg_data_t;

struct da16k_msg_t {
    size_t                  data_count;
    size_t                  data_capacity;
    da16k_msg_data_t       *data;
};

static char da16k_value_buffer[64] = {0};

static uint32_t s_network_timeout_ms        = DA16K_DEFAULT_IOTC_TIMEOUT_MS;
static uint32_t s_iotc_connect_timeout_ms   = DA16K_DEFAULT_IOTC_CONNECT_TIMEOUT_MS;

da16k_err_t da16k_get_cmd(da16k_cmd_t *cmd) {
    const char  expected_response[] = "+NWICGETCMD";
    const char  at_message[]        = "AT+NWICGETCMD";
    char       *param_ptr           = NULL;
    da16k_err_t ret                 = DA16K_SUCCESS;

    DA16K_RETURN_ON_NULL(DA16K_INVALID_PARAMETER, cmd);

    ret = da16k_at_send_formatted_msg(at_message);

    if (ret != DA16K_SUCCESS) {
        DA16K_ERROR("Error sending message: %d\r\n", (int) ret);
        return ret;
    }
    
    ret = da16k_at_receive_and_validate_response(true, expected_response, DA16K_UART_TIMEOUT_MS);

    if (ret != DA16K_SUCCESS && ret != DA16K_AT_ERROR_CODE) {
        return ret;
    }

    if (ret == DA16K_AT_ERROR_CODE) {
        /* We have received an error response, which usually means there are no commands ("ERROR:7"). */

        if (da16k_at_get_response_code() == -7) {
            DA16K_DEBUG("No commands available.\r\n");
            return DA16K_NO_CMDS;
        } else {
            DA16K_ERROR("Bad response, error code %d\r\n", da16k_at_get_response_code());
            return DA16K_AT_FAIL;
        }
    }

    /* Now we need to extract the command and parameter (if applicable) */
    cmd->command = da16k_at_get_response_str();
    DA16K_RETURN_ON_NULL(DA16K_OUT_OF_MEMORY, cmd->command);

    ret = DA16K_SUCCESS;

    /* Find space to determine whether we have params or not. We can use strchr here as we're guaranteed a null terminator within the buffer */
    param_ptr = strchr(cmd->command, ' ');

    if (param_ptr != NULL) {
        /* We have params, split the strings */
        *param_ptr = 0x00; /* Null terminate via the space so we can simply use strdup */
        param_ptr++; /* Skip space/null */

        /* cmd->command stays as-is and now termintes after the command itself. We *do* waste a few bytes of memory */
        cmd->parameters = da16k_strdup(param_ptr);

        if (cmd->parameters == NULL) {
            da16k_destroy_cmd(*cmd);
            ret = DA16K_OUT_OF_MEMORY;
        }
    } else {
        /* No parameter, just command */
        cmd->parameters = NULL;
    }

    return ret;
}

void da16k_destroy_cmd(da16k_cmd_t cmd) {
    if (cmd.command)
        da16k_free(cmd.command);
    if (cmd.parameters)
        da16k_free(cmd.parameters);
}

da16k_err_t da16k_init(const da16k_cfg_t *cfg) {
    da16k_err_t ret = DA16K_SUCCESS;

    DA16K_RETURN_ON_NULL(DA16K_INVALID_PARAMETER, cfg);

#if defined(static_assert)
    static_assert(sizeof(double) == 8 && sizeof(float) == 4, "Unexpected floating point size, check your compiler/c-library!");
#else
    if (sizeof(double) != 8 || sizeof(float) != 4) {
        DA16K_PRINT("Your platform has unexpected floating point formats!\r\n");
        DA16K_PRINT("   (sizeof(double) expected %u, have %u)\r\n", 8, sizeof(double));
        DA16K_PRINT("   (sizeof(float)  expected %u, have %u)\r\n", 4, sizeof(float));
        return DA16K_NOT_INITIALIZED;
    }
#endif

    /* UART Init */
    if (!da16k_uart_init()) {
        return DA16K_UART_ERROR;
    }

    /* WiFi init (if requested) */
    if (cfg->wifi_config) {
        if (DA16K_SUCCESS != (ret = da16k_set_wifi_config(cfg->wifi_config))) {
            DA16K_ERROR("WiFi connection failed (%d)\r\n", (int) ret);
            return ret;
        }
    }

    /* IoTC init (if requested) */
    if (cfg->iotc_config) {
        if (DA16K_SUCCESS != (ret = da16k_setup_iotc_and_connect(cfg->iotc_config))) {
            DA16K_ERROR("IoTC connection failed (%d)\r\n", (int) ret);
            return ret;
        }
        if (cfg->iotc_config->iotc_connect_timeout_ms) {
            s_iotc_connect_timeout_ms = cfg->iotc_config->iotc_connect_timeout_ms;
        }
    }

    /* External network timeout override */
    if (cfg->network_timeout_ms) {
        s_network_timeout_ms = cfg->network_timeout_ms;
    }

    return ret; /* TODO: Check if IoTC is actually connected. */
}

void da16k_deinit() {
    da16k_uart_close();
}

da16k_msg_t *da16k_create_msg(void) {
    da16k_msg_t *ret = da16k_malloc(sizeof(da16k_msg_t));

    DA16K_RETURN_ON_NULL(NULL, ret);

    memset(ret, 0, sizeof(da16k_msg_t));

    return ret;
}

/*  Adds a da16k_msg_data_t entryto a da16k_msg_t's data array.

    Will (re-) allocate if capacity is exceeded.

    In an out of memory condition, the data is not added, but the previous data remains valid.

    Do note that no checks are performed on data's content, it is only checked if data is NULL. */
static da16k_err_t da16k_msg_add_internal(da16k_msg_t *msg, const da16k_msg_data_t *data) {
    da16k_msg_data_t   *new_data        = NULL;
    size_t              new_capacity;

    DA16K_RETURN_ON_NULL(DA16K_INVALID_PARAMETER, msg);

    /* If we're out of capacity for data, we need to realloc with bigger capacity */
    if (msg->data_count == msg->data_capacity) {
        DA16K_DEBUG("msg->data capacity reached, reallocating...\r\n");

        new_capacity = msg->data_capacity + 10;
        new_data = da16k_malloc(sizeof(da16k_msg_data_t) * new_capacity);

        if (new_data == NULL) {
            DA16K_ERROR("Out of memory!\r\n");
            return DA16K_OUT_OF_MEMORY;
        }

        if (msg->data) {
            memcpy(new_data, msg->data, sizeof(da16k_msg_data_t) * msg->data_capacity);
            da16k_free(msg->data);
        }

        msg->data_capacity = new_capacity;
        msg->data = new_data;
    }

    /* Now apply new data to the next free slot */

    memcpy(&msg->data[msg->data_count], data, sizeof(da16k_msg_data_t));
    msg->data_count++;

    return DA16K_SUCCESS;
}


da16k_err_t da16k_msg_add_str(da16k_msg_t *msg, const char *key, const char *value) {
    da16k_msg_data_t data = {0};

    DA16K_RETURN_ON_NULL(DA16K_INVALID_PARAMETER, msg);
    DA16K_RETURN_ON_NULL(DA16K_INVALID_PARAMETER, key);
    DA16K_RETURN_ON_NULL(DA16K_INVALID_PARAMETER, value);

    data.key                = da16k_strdup(key);
    data.type               = DA16K_AT_STRING;
    data.value.d_string     = data.key ? da16k_strdup(value) : NULL; /* little hack so we can use the helper macro to exit the function cleanly on OOM */

    DA16K_RETURN_ON_NULL(DA16K_OUT_OF_MEMORY, key);
    DA16K_RETURN_ON_NULL(DA16K_OUT_OF_MEMORY, data.value.d_string);

    return da16k_msg_add_internal(msg, &data);
}

da16k_err_t da16k_msg_add_bool(da16k_msg_t *msg, const char *key, bool value) {
    da16k_msg_data_t data = {0};

    DA16K_RETURN_ON_NULL(DA16K_INVALID_PARAMETER, msg);
    DA16K_RETURN_ON_NULL(DA16K_INVALID_PARAMETER, key);

    data.key                = da16k_strdup(key);
    data.type               = DA16K_AT_BOOL;
    data.value.d_bool       = value;

    DA16K_RETURN_ON_NULL(DA16K_OUT_OF_MEMORY, key);

    return da16k_msg_add_internal(msg, &data);
}

da16k_err_t da16k_msg_add_num(da16k_msg_t *msg, const char *key, double value) {
    da16k_msg_data_t data = {0};

    DA16K_RETURN_ON_NULL(DA16K_INVALID_PARAMETER, msg);
    DA16K_RETURN_ON_NULL(DA16K_INVALID_PARAMETER, key);

    data.key                = da16k_strdup(key);
    data.type               = DA16K_AT_FLOAT64;
    data.value.d_float64    = value;

    DA16K_RETURN_ON_NULL(DA16K_OUT_OF_MEMORY, key);

    return da16k_msg_add_internal(msg, &data);
}

/* Sends a single piece of telemetry data as part of a larger command transmission */
static da16k_err_t da16k_send_msg_data(const da16k_msg_data_t *data) {
    DA16K_RETURN_ON_NULL(DA16K_INVALID_PARAMETER, data);
    DA16K_RETURN_ON_NULL(DA16K_INVALID_PARAMETER, data->key);

    switch (data->type) {
        case DA16K_AT_STRING:
            DA16K_RETURN_ON_NULL(DA16K_INVALID_PARAMETER, data->value.d_string);
            return      da16k_at_send_formatted_raw_no_crlf("%d,%s,%s,", (int) data->type, data->key, data->value.d_string);
        case DA16K_AT_BOOL:
            if (da16k_bool_to_ascii_hex(da16k_value_buffer, data->value.d_bool))
                return  da16k_at_send_formatted_raw_no_crlf("%d,%s,%s,", (int) data->type, data->key, da16k_value_buffer);
            break;
        case DA16K_AT_FLOAT64:
            if (da16k_double_to_ascii_hex(da16k_value_buffer, data->value.d_float64))
                return  da16k_at_send_formatted_raw_no_crlf("%d,%s,%s,", (int) data->type, data->key, da16k_value_buffer);
            break;
        default:
            break;
    }

    return DA16K_INVALID_PARAMETER;
}

da16k_err_t da16k_send_msg (const da16k_msg_t *msg) {
    da16k_err_t ret = DA16K_SUCCESS;

    DA16K_RETURN_ON_NULL(DA16K_INVALID_PARAMETER, msg);
    DA16K_RETURN_ON_NULL(DA16K_INVALID_PARAMETER, msg->data);

    /* Iterate through all data tuples */
    for (size_t i = 0; i < msg->data_count; i++) {
        /* If we're at the start of a bulk message initiate it, note that no CRLF is sent here. Space is important...*/
        if ((i % DA16K_MSG_TUPLES_PER_ITERATION) == 0 && DA16K_SUCCESS != (ret = da16k_at_send_formatted_raw_no_crlf("AT+NWICEXMSG "))) {
            DA16K_ERROR("Failed to initiate message\r\n");
            break;
        }

        /* Send actual tuple data */
        if (DA16K_SUCCESS != (ret = da16k_send_msg_data(&msg->data[i]))) {
            DA16K_ERROR("Failed to send message tuple data\r\n");
            break;
        }

        /* Maximum of DA16K_MSG_TUPLES_PER_ITERATION tuples at a time & force send if it's the last message */
        if ((i % DA16K_MSG_TUPLES_PER_ITERATION) == (DA16K_MSG_TUPLES_PER_ITERATION - 1) || i == (msg->data_count - 1)) {
            /* Finalize with empty string, \r\n will be added by this function */
            if (DA16K_SUCCESS != (ret = da16k_at_send_formatted_and_check_success(s_network_timeout_ms, "+NWICEXMSG", ""))) {
                DA16K_ERROR("Failed to finalize/validate message\r\n");
                break;
            }
        }
    }

    return ret;
}

void da16k_destroy_msg(da16k_msg_t *msg) {
    if (msg) {
        if (msg->data) {
            for (size_t i = 0; i < msg->data_count; i++) {
                da16k_free((void*) msg->data[i].key);

                if (msg->data[i].type == DA16K_AT_STRING) {
                    da16k_free((void*) msg->data[i].value.d_string);
                }
            }

            da16k_free(msg->data);
        }
        da16k_free(msg);
    }
}

/* Helper functions for direct sending (for basic, non-threaded applications) */

da16k_err_t da16k_send_msg_direct_str(const char *key, const char *value) {
    da16k_msg_data_t    data    = { .key = key,
                                    .type = DA16K_AT_STRING,
                                    .value.d_string = (char *) value };
    da16k_msg_t         msg     = { .data = &data,
                                    .data_capacity = 1,
                                    .data_count = 1 };

    /* send_msg and send_msg_data do error checking, so not needed here */
    return da16k_send_msg(&msg);
}

da16k_err_t da16k_send_msg_direct_bool(const char *key, bool value) {
    da16k_msg_data_t    data    = { .key = key,
                                    .type = DA16K_AT_BOOL,
                                    .value.d_bool = value };
    da16k_msg_t         msg     = { .data = &data,
                                    .data_capacity = 1,
                                    .data_count = 1 };
    return da16k_send_msg(&msg);
}

da16k_err_t da16k_send_msg_direct_num(const char *key, double value) {
    da16k_msg_data_t    data    = { .key = key,
                                    .type = DA16K_AT_FLOAT64,
                                    .value.d_float64 = value };
    da16k_msg_t         msg     = { .data = &data,
                                    .data_capacity = 1,
                                    .data_count = 1 };
    return da16k_send_msg(&msg);
}

da16k_err_t da16k_set_iotc_connection_type(da16k_iotc_mode_t type) {
    return da16k_at_send_formatted_and_check_success(DA16K_UART_TIMEOUT_MS, NULL, "AT+NWICCT %u", (unsigned) type);
}

da16k_err_t da16k_set_iotc_auth_type(da16k_iotc_auth_type_t type) {
    return da16k_at_send_formatted_and_check_success(DA16K_UART_TIMEOUT_MS, NULL, "AT+NWICAT %u", (unsigned) type);
}

da16k_err_t da16k_set_iotc_cpid(const char *cpid) {
    DA16K_RETURN_ON_NULL(DA16K_INVALID_PARAMETER, cpid);
    return da16k_at_send_formatted_and_check_success(DA16K_UART_TIMEOUT_MS, NULL, "AT+NWICCPID %s", cpid);
}

da16k_err_t da16k_set_iotc_duid(const char *duid) {
    DA16K_RETURN_ON_NULL(DA16K_INVALID_PARAMETER, duid);
    return da16k_at_send_formatted_and_check_success(DA16K_UART_TIMEOUT_MS, NULL, "AT+NWICDUID %s", duid);
}

da16k_err_t da16k_set_iotc_env(const char *env) {
    DA16K_RETURN_ON_NULL(DA16K_INVALID_PARAMETER, env);
    return da16k_at_send_formatted_and_check_success(DA16K_UART_TIMEOUT_MS, NULL, "AT+NWICENV %s", env);
}

da16k_err_t da16k_iotc_start(void) {
    da16k_err_t ret = DA16K_SUCCESS;
    
    /* Starting consists of two parts: the setup and the actual start. Since decoupling the two from our POV is pointless,
       we do both of these in this wrapper. */

    ret = da16k_at_send_formatted_and_check_success(s_network_timeout_ms, "+NWICSETUPEND", "AT+NWICSETUP");
    if (ret == DA16K_SUCCESS) {
        ret = da16k_at_send_formatted_and_check_success(s_iotc_connect_timeout_ms, "+NWICSTARTEND", "AT+NWICSTART");
    }
    return ret;
}

da16k_err_t da16k_iotc_stop(void) {
    return da16k_at_send_formatted_and_check_success(s_network_timeout_ms, "+NWICSTOPEND", "AT+NWICSTOP");
}

da16k_err_t da16k_iotc_reset(void) {
    return da16k_at_send_formatted_and_check_success(s_network_timeout_ms, "+NWICRESETEND", "AT+NWICRESET");
}

da16k_err_t da16k_set_wifi_config(const da16k_wifi_cfg_t *cfg) {
    DA16K_RETURN_ON_NULL(DA16K_INVALID_PARAMETER, cfg);
    DA16K_RETURN_ON_NULL(DA16K_INVALID_PARAMETER, cfg->ssid);

    /* Existing IoTC session must be stopped first to avoid reconnection attempts while wifi is connecting.
       Return codes can be ignored for this as when there is no connection in place, only "OK" will arrive. */
    da16k_iotc_stop();

    return da16k_at_send_formatted_and_check_success(
        cfg->wifi_connect_timeout_ms ? cfg->wifi_connect_timeout_ms : DA16K_DEFAULT_WIFI_TIMEOUT_MS,    /* Timeout, if present */
        "+WFJAP", "AT+WFJAPA %s,%s,%d", /* AT Command*/
        cfg->ssid,                      /* SSID */
        cfg->key ? cfg->key : "",       /* Passphrase if present, blank if not */
        cfg->hidden ? 1 : 0);           /* Hidden network flag */
}

da16k_err_t da16k_set_device_cert(const char *cert, const char *key) {
    da16k_err_t ret = DA16K_SUCCESS;

    DA16K_RETURN_ON_NULL(DA16K_INVALID_PARAMETER, cert);
    DA16K_RETURN_ON_NULL(DA16K_INVALID_PARAMETER, key);

    DA16K_WARN("WARNING: Client certificate transmission via the AT command protocol is *INSECURE* and may ONLY be used for testing / development purposes!\r\n");

    /* MQTT Client Certificate */
    if (DA16K_SUCCESS != (ret = da16k_at_send_certificate(DA16K_CERT_MQTT_DEV_CERT, cert))) { return ret; }
    /* MQTT Client Private Key */
    if (DA16K_SUCCESS != (ret = da16k_at_send_certificate(DA16K_CERT_MQTT_DEV_KEY, key)))   { return ret; }

    return ret;
}

da16k_err_t da16k_setup_iotc_and_connect(const da16k_iotc_cfg_t *cfg) {
    da16k_err_t ret = DA16K_SUCCESS;

    DA16K_RETURN_ON_NULL(DA16K_INVALID_PARAMETER, cfg);
    DA16K_RETURN_ON_NULL(DA16K_INVALID_PARAMETER, cfg->cpid);
    DA16K_RETURN_ON_NULL(DA16K_INVALID_PARAMETER, cfg->duid);
    DA16K_RETURN_ON_NULL(DA16K_INVALID_PARAMETER, cfg->env);

    if (DA16K_SUCCESS != (ret = da16k_iotc_stop()))                                             { return ret; }

    if (DA16K_SUCCESS != (ret = da16k_set_iotc_connection_type(cfg->mode)))                     { return ret; }
    if (DA16K_SUCCESS != (ret = da16k_set_iotc_cpid(cfg->cpid)))                                { return ret; }
    if (DA16K_SUCCESS != (ret = da16k_set_iotc_duid(cfg->duid)))                                { return ret; }
    if (DA16K_SUCCESS != (ret = da16k_set_iotc_env(cfg->env)))                                  { return ret; }
    if (DA16K_SUCCESS != (ret = da16k_set_iotc_auth_type(DA16K_IOTC_AT_X509)))                  { return ret; }

    if (cfg->device_cert) {
        if (DA16K_SUCCESS != (ret = da16k_set_device_cert(cfg->device_cert, cfg->device_key)))  { return ret; }
    }

    if (DA16K_SUCCESS != (ret = da16k_iotc_reset()))                                            { return ret; }
    if (DA16K_SUCCESS != (ret = da16k_iotc_start()))                                            { return ret; }

    return ret;
}
