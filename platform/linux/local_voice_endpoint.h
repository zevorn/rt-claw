/* SPDX-License-Identifier: MIT */

#ifndef PLATFORM_LINUX_LOCAL_VOICE_ENDPOINT_H
#define PLATFORM_LINUX_LOCAL_VOICE_ENDPOINT_H

#ifdef __cplusplus
extern "C" {
#endif

int local_voice_endpoint_init(void);
int local_voice_endpoint_start(void);
void local_voice_endpoint_stop(void);
int local_voice_endpoint_running(void);
int local_voice_endpoint_capture_start(void);
int local_voice_endpoint_capture_stop(void);
int local_voice_endpoint_cancel(void);
int local_voice_endpoint_set_input(const char *device);
int local_voice_endpoint_set_output(const char *device);
const char *local_voice_endpoint_get_input(void);
const char *local_voice_endpoint_get_output(void);

#ifdef __cplusplus
}
#endif

#endif /* PLATFORM_LINUX_LOCAL_VOICE_ENDPOINT_H */
