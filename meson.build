project('rt-claw', 'c',
    version: '0.1.0',
    default_options: [
        'c_std=gnu99',
        'warning_level=2',
    ],
)

osal_backend = get_option('osal')
project_root = meson.project_source_root()

# Platform-specific include directories and compile definitions
platform_defs = []
platform_incs = []

if osal_backend == 'rtthread'
    rtthread_bsp = get_option('rtthread_bsp')
    rtthread_cpuport = get_option('rtthread_cpuport')
    platform_defs += ['-D_POSIX_SOURCE', '-DCLAW_PLATFORM_RTTHREAD']
    rtt = project_root / 'vendor' / 'rt-thread'
    platform_incs += include_directories(
        'vendor/os/rt-thread/include',
        rtthread_cpuport,
        'vendor/os/rt-thread/components/legacy',
        'vendor/os/rt-thread/components/finsh',
        'vendor/os/rt-thread/components/net/lwip/port',
        'vendor/os/rt-thread/components/net/lwip/port/netif',
        'vendor/os/rt-thread/components/libc/compilers/common/include',
        'vendor/os/rt-thread/components/libc/compilers/newlib',
        'vendor/os/rt-thread/components/libc/posix/io/poll',
        'vendor/os/rt-thread/components/libc/posix/io/stdio',
        'vendor/os/rt-thread/components/libc/posix/ipc',
        'vendor/os/rt-thread/components/drivers/include',
        'vendor/os/rt-thread/components/net/sal/include',
        'vendor/os/rt-thread/components/net/sal/include/socket',
        'vendor/os/rt-thread/components/net/sal/include/socket/sys_socket',
        'vendor/os/rt-thread/components/net/netdev/include',
        'vendor/os/rt-thread/components/net/lwip/lwip-2.1.2/src/include',
        'vendor/os/rt-thread/components/net/lwip/lwip-2.1.2/src/include/netif',
        rtthread_bsp,
        rtthread_bsp / 'drivers',
        is_system: true,
    )
elif osal_backend == 'freertos'
    # ESP-IDF defines and include paths are provided by the auto-generated
    # cross.ini (scripts/gen-esp32c3-cross.py).  No platform_incs needed.
    platform_defs += ['-DCLAW_PLATFORM_ESP_IDF']
endif

# ---- Unified configuration via claw_config_generated.h ----
#
# All rt-claw credentials and tunable parameters are configured here,
# NOT in vendor SDK configs (ESP-IDF Kconfig, RT-Thread rtconfig).
#
# Priority: meson option (-Dai_api_key=...) > env var (RTCLAW_AI_API_KEY)
#           > claw_config.h #ifndef default.
#
# String config: [meson_option, env_var_name, config_macro_name]
_str_overrides = {
    'ai_api_key':       ['RTCLAW_AI_API_KEY',       'CONFIG_RTCLAW_AI_API_KEY'],
    'ai_api_url':       ['RTCLAW_AI_API_URL',       'CONFIG_RTCLAW_AI_API_URL'],
    'ai_model':         ['RTCLAW_AI_MODEL',         'CONFIG_RTCLAW_AI_MODEL'],
    'feishu_app_id':    ['RTCLAW_FEISHU_APP_ID',    'CONFIG_RTCLAW_FEISHU_APP_ID'],
    'feishu_app_secret':['RTCLAW_FEISHU_APP_SECRET','CONFIG_RTCLAW_FEISHU_APP_SECRET'],
}

# Integer config: [meson_option, env_var_name, config_macro_name]
_int_overrides = {
    'ai_max_tokens':      ['RTCLAW_AI_MAX_TOKENS',      'CONFIG_RTCLAW_AI_MAX_TOKENS'],
    'ai_context_size':    ['RTCLAW_AI_CONTEXT_SIZE',     'CONFIG_RTCLAW_AI_CONTEXT_SIZE'],
    'ai_memory_max_msgs': ['RTCLAW_AI_MEMORY_MAX_MSGS',  'CONFIG_RTCLAW_AI_MEMORY_MAX_MSGS'],
}

conf_data = configuration_data()
_has_any = false

foreach opt_name, pair : _str_overrides
    _val = get_option(opt_name)
    if _val == ''
        _r = run_command('sh', '-c',
            'printf "%s" "${' + pair[0] + ':-}"', check: false)
        _val = _r.stdout().strip()
    endif
    if _val != ''
        conf_data.set_quoted(pair[1], _val)
        _has_any = true
    endif
endforeach

foreach opt_name, pair : _int_overrides
    _val = get_option(opt_name)
    if _val > 0
        conf_data.set(pair[1], _val)
        _has_any = true
    else
        _r = run_command('sh', '-c',
            'printf "%s" "${' + pair[0] + ':-}"', check: false)
        _env = _r.stdout().strip()
        if _env != ''
            conf_data.set(pair[1], _env.to_int())
            _has_any = true
        endif
    endif
endforeach

claw_config_gen = configure_file(
    input: 'claw_gen_config.h.in',
    output: 'claw_gen_config.h',
    configuration: conf_data,
)

# Tell claw_config.h to include the generated header
platform_defs += ['-DCLAW_HAS_GENERATED_CONFIG']

# Unified header directories:
#   - project root: claw_config.h
#   - include/: all other public headers
#   - build dir: claw_gen_config.h (added implicitly by Meson)
claw_root_inc = include_directories('.')
claw_inc = include_directories('include')

subdir('vendor/lib/cjson')
subdir('osal')
subdir('claw')
