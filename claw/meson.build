src_files = files(
    'claw_init.c',
    'core/gateway.c',
    'core/scheduler.c',
    'services/ai/ai_engine.c',
    'services/ai/ai_memory.c',
    'services/ai/ai_skill.c',
    'services/im/feishu.c',
    'services/im/telegram.c',
    'services/net/net_service.c',
    'services/swarm/swarm.c',
    'shell/shell_commands.c',
    'tools/claw_tools.c',
    'tools/tool_gpio.c',
    'tools/tool_lcd.c',
    'tools/tool_sched.c',
    'tools/tool_system.c',
    'tools/tool_net.c',
)

# Drivers (outside claw/ directory)
src_files += files(
    '../drivers/display/espressif/ssd1306_oled.c',
    '../drivers/audio/espressif/es8311_audio.c',
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
endif
if get_option('tool_system')
    feature_defs += '-DCONFIG_RTCLAW_TOOL_SYSTEM'
endif
if get_option('tool_sched')
    feature_defs += '-DCONFIG_RTCLAW_TOOL_SCHED'
endif
if get_option('tool_net')
    feature_defs += '-DCONFIG_RTCLAW_TOOL_NET'
endif
if get_option('heartbeat')
    if not get_option('sched')
        error('heartbeat requires sched=true')
    endif
    feature_defs += '-DCONFIG_RTCLAW_HEARTBEAT_ENABLE'
    src_files += files('core/heartbeat.c')
endif
if get_option('feishu')
    feature_defs += '-DCONFIG_RTCLAW_FEISHU_ENABLE'
endif
if get_option('telegram')
    feature_defs += '-DCONFIG_RTCLAW_TELEGRAM_ENABLE'
endif

rtclaw_lib = static_library('rtclaw', src_files,
    include_directories: [claw_root_inc, claw_inc] + platform_incs,
    dependencies: [osal_dep, cjson_dep],
    c_args: feature_defs,
)

rtclaw_dep = declare_dependency(
    link_with: rtclaw_lib,
    include_directories: [claw_root_inc, claw_inc],
)
