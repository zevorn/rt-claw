/*
 * Copyright (c) 2026, Chao Liu <chao.liu.zevorn@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Unified error codes for rt-claw.
 * Replaces mixed CLAW_OK/CLAW_ERROR/esp_err_t/int conventions.
 */

#ifndef CLAW_CORE_CLAW_ERRNO_H
#define CLAW_CORE_CLAW_ERRNO_H

typedef enum {
    CLAW_OK          =  0,    /* success */
    CLAW_ERR_GENERIC = -1,    /* unspecified error */
    CLAW_ERR_NOMEM   = -2,    /* out of memory */
    CLAW_ERR_TIMEOUT = -3,    /* operation timed out */
    CLAW_ERR_INVALID = -4,    /* invalid argument or parameter */
    CLAW_ERR_STATE   = -5,    /* invalid state for this operation */
    CLAW_ERR_BUSY    = -6,    /* resource is busy */
    CLAW_ERR_NOENT   = -7,    /* entry not found */
    CLAW_ERR_IO      = -8,    /* I/O or network error */
    CLAW_ERR_FULL    = -9,    /* registry or buffer is full */
    CLAW_ERR_EXIST   = -10,   /* entry already exists */
    CLAW_ERR_DEPEND  = -11,   /* dependency not satisfied */
    CLAW_ERR_CYCLE   = -12,   /* circular dependency detected */
} claw_err_t;

/*
 * Return a human-readable string for an error code.
 * Never returns NULL; unknown codes map to "unknown error".
 */
const char *claw_strerror(claw_err_t err);

#endif /* CLAW_CORE_CLAW_ERRNO_H */
