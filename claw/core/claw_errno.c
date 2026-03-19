/*
 * Copyright (c) 2026, Chao Liu <chao.liu.zevorn@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#include "claw/core/claw_errno.h"

const char *claw_strerror(claw_err_t err)
{
    switch (err) {
    case CLAW_OK:          return "success";
    case CLAW_ERR_GENERIC: return "generic error";
    case CLAW_ERR_NOMEM:   return "out of memory";
    case CLAW_ERR_TIMEOUT: return "timeout";
    case CLAW_ERR_INVALID: return "invalid argument";
    case CLAW_ERR_STATE:   return "invalid state";
    case CLAW_ERR_BUSY:    return "resource busy";
    case CLAW_ERR_NOENT:   return "not found";
    case CLAW_ERR_IO:      return "I/O error";
    case CLAW_ERR_FULL:    return "full";
    case CLAW_ERR_EXIST:   return "already exists";
    case CLAW_ERR_DEPEND:  return "dependency not satisfied";
    case CLAW_ERR_CYCLE:   return "circular dependency";
    default:               return "unknown error";
    }
}
