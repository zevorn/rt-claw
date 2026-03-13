src_files = files(
    'claw_init.c',
    'core/gateway.c',
    'core/scheduler.c',
    'services/ai/ai_engine.c',
    'services/ai/ai_memory.c',
    'services/ai/ai_skill.c',
    'services/im/feishu.c',
    'services/net/net_service.c',
    'services/swarm/swarm.c',
    'tools/claw_tools.c',
    'tools/tool_gpio.c',
    'tools/tool_lcd.c',
    'tools/tool_sched.c',
    'tools/tool_system.c',
    'tools/tool_net.c',
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

rtclaw_lib = static_library('rtclaw', src_files,
    include_directories: [claw_inc] + platform_incs,
    dependencies: [osal_dep, cjson_dep],
    c_args: feature_defs,
)

rtclaw_dep = declare_dependency(
    link_with: rtclaw_lib,
    include_directories: claw_inc,
)
