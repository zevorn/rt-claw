#ifndef CLAW_SERVICES_VOICE_SERVICE_H
#define CLAW_SERVICES_VOICE_SERVICE_H

#include "osal/claw_os.h"
#include "claw/core/errno.h"
#include "claw/services/voice/voice_endpoint.h"
#include "claw/services/voice/voice_providers.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Service lifecycle (registered via CLAW_SERVICE_REGISTER) */
int voice_service_init(void);
int voice_service_start(void);
void voice_service_stop(void);

claw_err_t voice_config_set_enabled(int enabled);
int voice_config_get_enabled(void);
claw_err_t voice_config_set_web_port(int port);
int voice_config_get_web_port(void);
claw_err_t voice_config_set_string(const char *key, const char *value);
claw_err_t voice_config_snapshot(voice_runtime_config_t *cfg);
int voice_state_get(void);
const char *voice_state_name(int state);
claw_err_t voice_submit_event(const struct voice_endpoint_event *event);

#ifdef __cplusplus
}
#endif

#endif /* CLAW_SERVICES_VOICE_SERVICE_H */
