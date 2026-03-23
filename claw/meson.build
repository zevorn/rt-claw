src_files = files(
    'init.c',
    'core/errno.c',
    'core/console.c',
    'core/service.c',
    'core/driver.c',
    'core/tool.c',
    'services/gateway.c',
    'services/sched.c',
    'services/ai/ai_engine.c',
    'services/ai/ai_memory.c',
    'services/ai/ai_skill.c',
    'services/im/im_util.c',
    'services/im/feishu.c',
    'services/im/telegram.c',
    'services/net/net_service.c',
    'services/swarm/swarm.c',
    'shell/shell_commands.c',
    'shell/shell_history.c',
    'services/tools/tools.c',
    'services/tools/lcd.c',
    'services/tools/audio.c',
)

# Drivers (outside claw/ directory)
# es8311_audio.c excluded: depends on esp_codec_dev managed component
# which has transitive headers not available in Meson cross-compile.
# It compiles via ESP-IDF CMake (platform/esp32c3/main/CMakeLists.txt).
src_files += files(
    '../drivers/display/espressif/ssd1306_oled.c',
)

# Feature flags
feature_defs = platform_defs
if get_option('swarm')
    feature_defs += '-DCONFIG_RTCLAW_SWARM_ENABLE'
endif
if get_option('sched')
    feature_defs += '-DCONFIG_RTCLAW_SCHED_ENABLE'
endif
if get_option('skill')
    feature_defs += '-DCONFIG_RTCLAW_SKILL_ENABLE'
endif
if get_option('tool_gpio')
    feature_defs += '-DCONFIG_RTCLAW_TOOL_GPIO'
    src_files += files('services/tools/gpio.c')
endif
if get_option('tool_system')
    feature_defs += '-DCONFIG_RTCLAW_TOOL_SYSTEM'
    src_files += files('services/tools/system.c')
endif
if get_option('tool_sched')
    feature_defs += '-DCONFIG_RTCLAW_TOOL_SCHED'
endif
# sched.c provides both LLM tools (guarded by CONFIG_RTCLAW_TOOL_SCHED)
# and helpers used by the scheduler service and shell (e.g.
# sched_tool_remove_by_name, sched_set_reply_context).
if get_option('tool_sched') or get_option('sched')
    src_files += files('services/tools/sched.c')
endif
if get_option('tool_net')
    feature_defs += '-DCONFIG_RTCLAW_TOOL_NET'
    src_files += files('services/tools/net.c')
endif
if get_option('tool_mouse')
    feature_defs += '-DCONFIG_RTCLAW_TOOL_MOUSE'
    src_files += files('services/tools/mouse.c')
endif
if get_option('heartbeat')
    if not get_option('sched')
        error('heartbeat requires sched=true')
    endif
    feature_defs += '-DCONFIG_RTCLAW_HEARTBEAT_ENABLE'
    src_files += files('services/heartbeat.c')
endif
if get_option('feishu')
    feature_defs += '-DCONFIG_RTCLAW_FEISHU_ENABLE'
endif
if get_option('telegram')
    feature_defs += '-DCONFIG_RTCLAW_TELEGRAM_ENABLE'
endif
if get_option('ota')
    feature_defs += '-DCONFIG_RTCLAW_OTA_ENABLE'
endif
# OTA sources always compiled; #ifdef CONFIG_RTCLAW_OTA_ENABLE inside
# controls whether code is active.  On ESP-IDF the flag comes from
# Kconfig (sdkconfig → cross.ini), on RT-Thread from Meson -Dota=true.
src_files += files(
    'services/ota/ota_service.c',
    'services/tools/ota.c',
)
# Non-ESP, non-Linux Meson platforms need an OTA platform stub.
# Linux provides its own in platform/linux/meson.build;
# ESP-IDF provides platform/common/espressif/esp_ota.c via CMake.
if osal_backend != 'linux'
    src_files += files('../platform/linux/ota_stub.c')
endif

rtclaw_lib = static_library('rtclaw', src_files,
    include_directories: [claw_root_inc, claw_inc] + platform_incs,
    dependencies: [osal_dep, cjson_dep],
    c_args: feature_defs,
)

rtclaw_dep = declare_dependency(
    link_whole: rtclaw_lib,
    include_directories: [claw_root_inc, claw_inc],
)
