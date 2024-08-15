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

#include "da16k_uart.h"

/* Default wifi connection timeout, 15 seconds */
#define DA16K_DEFAULT_WIFI_TIMEOUT_MS           15000
/* Default IoTC MQTT interaction timeout, 2 seconds */
#define DA16K_DEFAULT_IOTC_TIMEOUT_MS           2000
/* Default IoTC Connect timeout, 15 seconds */
#define DA16K_DEFAULT_IOTC_CONNECT_TIMEOUT_MS   15000

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

da16k_msg_t *da16k_create_msg_str(const char *key, const char *value) {
    da16k_msg_t *msg = da16k_malloc(sizeof(da16k_msg_t));

    DA16K_RETURN_ON_NULL(NULL, msg);
    DA16K_RETURN_ON_NULL(NULL, key);
    DA16K_RETURN_ON_NULL(NULL, value);

    msg->key     = da16k_strdup(key);
    msg->value   = da16k_strdup(value);

    if (!msg->key || !msg->value) {
        DA16K_ERROR("DA16K: Memory allocation for key/value failed!");
        da16k_destroy_msg(msg);
        return NULL;
    }

    return msg;
}

da16k_msg_t *da16k_create_msg_float(const char *key, double value) {
/*     platform might not support float printing :( Else we would do:
 *     snprintf(da16k_value_buffer, sizeof(da16k_value_buffer), "%f", value);*/
    if (!da16k_double_to_string(da16k_value_buffer, sizeof(da16k_value_buffer), value)) {
        DA16K_ERROR("%s: Double to string conversion failed!\r\n", __func__);
        return NULL;
    }
    return da16k_create_msg_str(key, da16k_value_buffer);
}

da16k_msg_t *da16k_create_msg_uint(const char *key, uint64_t value) {
    snprintf(da16k_value_buffer, sizeof(da16k_value_buffer), "%" PRIu64, value);
    return da16k_create_msg_str(key, da16k_value_buffer);
}

da16k_msg_t *da16k_create_msg_int(const char *key, int64_t value) {
    snprintf(da16k_value_buffer, sizeof(da16k_value_buffer), "%" PRIi64, value);
    return da16k_create_msg_str(key, da16k_value_buffer);
}

da16k_msg_t *da16k_create_msg_bool(const char *key, bool value) {
    snprintf(da16k_value_buffer, sizeof(da16k_value_buffer), value ? "true" : "false");
    return da16k_create_msg_str(key, da16k_value_buffer);
}

void da16k_destroy_msg(da16k_msg_t *msg) {
    if (msg) {
        da16k_free(msg->key);
        da16k_free(msg->value);
        da16k_free(msg);
    }
}

/* Helper functions for direct sending and destroying (for basic, non-threaded applications) */

/*  Sends a message directly and destroys it immediately after.
    Since it is used from within these wrappers after creation of msg, 
    it returns DA16K_OUT_OF_MEMORY on NULL pointers instead of DA16K_INVALID_PARAMETER as other functions. */
static da16k_err_t da16k_check_send_and_destroy_msg(da16k_msg_t *msg) {
    da16k_err_t ret;

    DA16K_RETURN_ON_NULL(DA16K_OUT_OF_MEMORY, msg);
    DA16K_RETURN_ON_NULL(DA16K_OUT_OF_MEMORY, msg->key);
    DA16K_RETURN_ON_NULL(DA16K_OUT_OF_MEMORY, msg->value);

    ret = da16k_send_msg(msg);
    da16k_destroy_msg(msg);
    return ret;
}

da16k_err_t da16k_send_msg_direct_str(const char *key, const char *value) {
    DA16K_RETURN_ON_NULL(DA16K_INVALID_PARAMETER, key);
    DA16K_RETURN_ON_NULL(DA16K_INVALID_PARAMETER, value);
    return da16k_check_send_and_destroy_msg(da16k_create_msg_str(key, value));
}

da16k_err_t da16k_send_msg_direct_float(const char *key, double value) {
    DA16K_RETURN_ON_NULL(DA16K_INVALID_PARAMETER, key);
    return da16k_check_send_and_destroy_msg(da16k_create_msg_float(key, value));
}

da16k_err_t da16k_send_msg_direct_uint(const char *key, uint64_t value) {
    DA16K_RETURN_ON_NULL(DA16K_INVALID_PARAMETER, key);
    return da16k_check_send_and_destroy_msg(da16k_create_msg_uint(key, value));
}

da16k_err_t da16k_send_msg_direct_int(const char *key, int64_t value) {
    DA16K_RETURN_ON_NULL(DA16K_INVALID_PARAMETER, key);
    return da16k_check_send_and_destroy_msg(da16k_create_msg_int(key, value));
}

da16k_err_t da16k_send_msg_direct_bool(const char *key, bool value) {
    DA16K_RETURN_ON_NULL(DA16K_INVALID_PARAMETER, key);
    return da16k_check_send_and_destroy_msg(da16k_create_msg_bool(key, value));
}

da16k_err_t da16k_send_msg(da16k_msg_t *msg) {
    DA16K_RETURN_ON_NULL(DA16K_INVALID_PARAMETER, msg);
    DA16K_RETURN_ON_NULL(DA16K_INVALID_PARAMETER, msg->key);
    DA16K_RETURN_ON_NULL(DA16K_INVALID_PARAMETER, msg->value);

    return da16k_at_send_formatted_and_check_success(s_network_timeout_ms, NULL, "AT+NWICMSG %s,%s", msg->key, msg->value);
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
