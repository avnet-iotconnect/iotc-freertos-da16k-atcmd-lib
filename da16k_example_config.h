/*
 * Example config for the da16k AT command library
 */

#ifndef DA16K_COMM_DA16K_CONFIG_H_
#define DA16K_COMM_DA16K_CONFIG_H_

/* Define a custom printf-style print function for debug messages (default is printf) */
#define DA16K_PRINT DebugPrint

/* Default allocator functions are malloc and free, however... */

/* This enables vpPortMalloc and vPortFree */
#define DA16K_CONFIG_FREERTOS

/* Soon there might be further (RT)OSs supported here */

/* This overrides any implied alloc functions */
#define DA16K_MALLOC_FN     MyMalloc
#define DA16K_FREE_FN       MyFree

#endif /* DA16K_COMM_DA16K_CONFIG_H_ */
