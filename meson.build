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

# Runtime config overrides.
# Priority: meson option (-Dai_api_key=...) > env var (RTCLAW_*) > claw_config.h default.
#
# For ESP-IDF (freertos): env vars are handled by gen-esp32*-cross.py which
# generates an env_override.h included AFTER sdkconfig.h — no -D conflict.
# Meson options still work for all platforms via -D flags below.
#
# Each entry: [env_var_name, config_macro_name]
_env_overrides = {
    'ai_api_key':       ['RTCLAW_AI_API_KEY',       'CONFIG_RTCLAW_AI_API_KEY'],
    'ai_api_url':       ['RTCLAW_AI_API_URL',       'CONFIG_RTCLAW_AI_API_URL'],
    'ai_model':         ['RTCLAW_AI_MODEL',         'CONFIG_RTCLAW_AI_MODEL'],
    'feishu_app_id':    ['RTCLAW_FEISHU_APP_ID',    'CONFIG_RTCLAW_FEISHU_APP_ID'],
    'feishu_app_secret':['RTCLAW_FEISHU_APP_SECRET','CONFIG_RTCLAW_FEISHU_APP_SECRET'],
}

foreach opt_name, pair : _env_overrides
    _val = get_option(opt_name)
    if _val == ''
        # For freertos (ESP-IDF), env vars are injected via cross.ini
        # override header; only read env here for non-ESP platforms.
        if osal_backend != 'freertos'
            _r = run_command('sh', '-c', 'printf "%s" "${' + pair[0] + ':-}"',
                             check: false)
            _val = _r.stdout().strip()
        endif
    endif
    if _val != ''
        _macro = pair[1]
        platform_defs += ['-U' + _macro, '-D' + _macro + '="' + _val + '"']
    endif
endforeach

# Unified header directory for all rt-claw public headers
claw_inc = include_directories('include')

subdir('vendor/lib/cjson')
subdir('osal')
subdir('claw')
