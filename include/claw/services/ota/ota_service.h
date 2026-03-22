/*
 * Copyright (c) 2026, Chao Liu <chao.liu.zevorn@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * OTA service — version check, update trigger, and lifecycle.
 */

#ifndef CLAW_OTA_SERVICE_H
#define CLAW_OTA_SERVICE_H

#include "platform/ota.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Service lifecycle (registered via CLAW_SERVICE_REGISTER) */
int  ota_service_init(void);
int  ota_service_start(void);
void ota_service_stop(void);

/*
 * Parse OTA version-check JSON into info struct.
 * @return CLAW_OK on success, CLAW_ERROR if version or url missing.
 */
int  ota_parse_version_json(const char *json, claw_ota_info_t *info);

/*
 * Semantic version comparison: "major.minor.patch".
 * @return >0 if a > b, 0 if equal, <0 if a < b.
 */
int  ota_version_compare(const char *a, const char *b);

/*
 * Check the configured OTA server for a newer firmware version.
 * Fills info on success.
 *
 * @return 1 if update available, 0 if up-to-date, negative on error.
 */
int  ota_check_update(claw_ota_info_t *info);

/*
 * Trigger an OTA update from the given URL.
 * Spawns a worker thread so the caller is not blocked.
 *
 * @return CLAW_OK if the update thread was created, CLAW_ERROR otherwise.
 */
int  ota_trigger_update(const char *url);

#ifdef __cplusplus
}
#endif

#endif /* CLAW_OTA_SERVICE_H */
