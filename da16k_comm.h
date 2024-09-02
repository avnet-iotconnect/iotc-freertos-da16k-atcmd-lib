/*
 * da16k_comm.h
 *
 * IoTConnect communication via DIALOG DA16600 PMOD wifi module
 *
 *  Created on: Dec 14, 2023
 *      Author: evoirin
 */

#ifndef DA16K_COMM_DA16K_COMM_H_
#define DA16K_COMM_DA16K_COMM_H_

#include <stdint.h>
#include <stdbool.h>

#if defined(DA16K_CONFIG_FILE)
#include DA16K_CONFIG_FILE
#else
#error "Please define DA16K_CONFIG_FILE!"
#endif

#include "da16k_platforms.h"

/* Enable generic printf */
#if !defined(DA16K_PRINT)
#include <stdio.h>
#define DA16K_PRINT             printf
#endif

/* FreeRTOS config helper */
#if defined(DA16K_CONFIG_FREERTOS)
#include "FreeRTOS.h"
#if !defined(DA16K_CONFIG_MALLOC_FN)
#define DA16K_CONFIG_MALLOC_FN pvPortMalloc
#endif

#if !defined(DA16K_CONFIG_FREE_FN)
#define DA16K_CONFIG_FREE_FN vPortFree
#endif
#endif

/* Generic malloc and free if none else are used */
#if !defined(DA16K_CONFIG_MALLOC_FN)
#include <malloc.h>
#define DA16K_CONFIG_MALLOC_FN malloc
#endif

#if !defined(DA16K_CONFIG_FREE_FN)
#define DA16K_CONFIG_FREE_FN free
#endif

typedef enum {
    DA16K_IOTC_AWS      = 1,
    DA16K_IOTC_AZURE    = 2,
} da16k_iotc_mode_t;

typedef enum {
    DA16K_IOTC_AT_X509    = 1
    /* Token not supported. */
} da16k_iotc_auth_type_t;

typedef struct {
    da16k_iotc_mode_t   mode;           /* IoTConnect Connection Type (AWS/Azure) */
    const char         *cpid;           /* IoTConnect CPID (Key Vault) */
    const char         *duid;           /* IoTConnect DUID (Key Vault) */
    const char         *env;            /* IoTConnect Environment setting (Key Vault) */

    uint32_t            iotc_connect_timeout_ms;        /* Timeout for IoTC connection (0 = Default) */
#define DA16K_DEFAULT_IOTC_CONNECT_TIMEOUT_MS   15000   /* Default: 15 seconds */

    /*  WARNING: CLIENT CERTIFICATE TRANSMISSION IS INSECURE AND THE FUNCTIONALITY
        IS ONLY PROVIDED FOR TESTING PURPOSES. */
    const char         *device_cert;    /* Device cert (NULL = rely on existing AT gateway configuration) */
    const char         *device_key;     /* Device key (must not be NULL if device_cert is used, ignored if device_cert is NULL) */
} da16k_iotc_cfg_t;

typedef struct {
    const char         *ssid;           /* WiFi network name */
    const char         *key;            /* WiFi network WPA/WPA2 passphrase - NULL for open network. */
    bool                hidden;         /* WiFi hidden network flag */

    uint32_t            wifi_connect_timeout_ms;        /* Timeout for WiFi connection in ms (0 = Default) */
#define DA16K_DEFAULT_WIFI_TIMEOUT_MS           15000   /* Default: 15 seconds */
} da16k_wifi_cfg_t;

typedef struct {
    da16k_iotc_cfg_t   *iotc_config;    /* IoTConnect device config (NULL = rely on existing AT gateway configuration) */
    da16k_wifi_cfg_t   *wifi_config;    /* (NULL = rely on existing AT gateway configuration) */

    uint32_t            network_timeout_ms;             /* Timeout for network operations e.g. confirmation on sending telemetry (0=Default) */
#define DA16K_DEFAULT_IOTC_TIMEOUT_MS           2000    /* Default: 2 seconds */
} da16k_cfg_t;

typedef enum e_da16k_err {
    DA16K_SUCCESS               = 0,    /* Operation was sucecssful */
    DA16K_OUT_OF_MEMORY         = 1,    /* A memory allocation in the requested operation has failed */
    DA16K_UART_ERROR            = 2,    /* The UART send/receive operation has failed */
    DA16K_TIMEOUT               = 3,    /* A timeout has occured communicating with the AT gateway */
    DA16K_AT_FAIL               = 4,    /* There was a failure communicating with the AT gateway */
    DA16K_AT_INVALID_MSG        = 5,    /* The message to be sent is invalid (bad pointer?) */
    DA16K_AT_MESSAGE_TOO_LONG   = 6,    /* The final, formatted message exceeds the TX buffer size. */
    DA16K_AT_RESPONSE_TOO_LONG  = 7,    /* The AT gateway has sent a line that exceeds the buffer limits */
    DA16K_AT_ERROR_CODE         = 8,    /* The AT message response was received correctly but contains an error code */
    DA16K_AT_NO_OK              = 9,    /* The AT message response was received but the "OK" marker was not */
    DA16K_NO_CMDS               = 10,   /* No new C2D commands have been sent to the device */
    DA16K_INVALID_PARAMETER     = 11,   /* The function was called with an invalid parameter */
    DA16K_NOT_INITIALIZED       = 12,   /* The initialization has failed or not occured yet */
} da16k_err_t;


typedef struct {
    char *command;
    char *parameters;
} da16k_cmd_t;

typedef struct da16k_msg_t da16k_msg_t;

/*  Init/deinit the library */
da16k_err_t da16k_init                      (const da16k_cfg_t *cfg);
void        da16k_deinit                    (void);

/* Create message */
da16k_msg_t *da16k_create_msg               (void);
/* Add data to message */
da16k_err_t da16k_msg_add_str               (da16k_msg_t *msg, const char *key, const char *value);
da16k_err_t da16k_msg_add_bool              (da16k_msg_t *msg, const char *key, bool value);
da16k_err_t da16k_msg_add_num               (da16k_msg_t *msg, const char *key, double value);
/*  Send data out via AT Commands (does not destroy the message!) */
da16k_err_t da16k_send_msg                  (const da16k_msg_t *msg);
/*  Destroy message & data */
void        da16k_destroy_msg               (da16k_msg_t *msg);

/*  Create message struct with given key and value, send it out, and destroy it. Can be used directly.
    This is intended for basic, non-threaded applications with ease-of-implementation in mind. */
da16k_err_t da16k_send_msg_direct_str       (const char *key, const char *value);
da16k_err_t da16k_send_msg_direct_bool      (const char *key, bool value);
da16k_err_t da16k_send_msg_direct_num       (const char *key, double value);

/*  IoTConnect configuration/setup
    These do not have to be called manually unless changed at runtime.
    da16k_init calls these.  */
da16k_err_t da16k_set_iotc_connection_type  (da16k_iotc_mode_t type);
da16k_err_t da16k_set_iotc_auth_type        (da16k_iotc_auth_type_t type);
da16k_err_t da16k_set_iotc_cpid             (const char *cpid);
da16k_err_t da16k_set_iotc_duid             (const char *duid);
da16k_err_t da16k_set_iotc_env              (const char *env);
da16k_err_t da16k_iotc_start                (void);
da16k_err_t da16k_iotc_stop                 (void);
da16k_err_t da16k_iotc_reset                (void);
da16k_err_t da16k_set_wifi_config           (const da16k_wifi_cfg_t *cfg);
da16k_err_t da16k_set_device_cert           (const char *cert, const char *key);
da16k_err_t da16k_setup_iotc_and_connect    (const da16k_iotc_cfg_t *cfg);

/*  Receives the next command from the AT command gateway.
    If DA16K_SUCCESS is returned, a command was fetched successfully (the struct must be destroyed after use).
    If DA16K_NO_CMDS is returned, no commands are available at this time.
    Other communication or memory-related error codes may occur. */
da16k_err_t da16k_get_cmd                   (da16k_cmd_t *cmd);
/*  Destroy command */
void        da16k_destroy_cmd               (da16k_cmd_t cmd);

#endif /* DA16K_COMM_DA16K_COMM_H_ */
