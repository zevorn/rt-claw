osal_backend = get_option('osal')

if osal_backend == 'rtthread'
    osal_src = files(
        'rtthread/claw_os_rtthread.c',
        'rtthread/claw_net_rtthread.c',
        'rtthread/claw_kv_rtthread.c',
    )
    osal_lib = static_library('osal', osal_src,
        include_directories: [claw_inc] + platform_incs,
        c_args: platform_defs,
    )
    osal_dep = declare_dependency(
        link_with: osal_lib,
        include_directories: claw_inc,
    )
elif osal_backend == 'freertos'
    osal_src = files(
        'freertos/claw_os_freertos.c',
        'freertos/claw_net_freertos.c',
        'freertos/claw_kv_freertos.c',
    )
    osal_lib = static_library('osal', osal_src,
        include_directories: [claw_inc] + platform_incs,
        c_args: platform_defs,
    )
    osal_dep = declare_dependency(
        link_with: osal_lib,
        include_directories: claw_inc,
    )
elif osal_backend == 'zephyr'
    osal_src = files(
        'zephyr/claw_os_zephyr.c',
        'zephyr/claw_net_zephyr.c',
        'zephyr/claw_kv_zephyr.c',
    )
    osal_lib = static_library('osal', osal_src,
        include_directories: [claw_inc] + platform_incs,
        c_args: platform_defs,
    )
    osal_dep = declare_dependency(
        link_with: osal_lib,
        include_directories: claw_inc,
    )
elif osal_backend == 'linux'
    libcurl_dep = dependency('libcurl', required: true)
    osal_src = files(
        'linux/claw_os_linux.c',
        'linux/claw_net_linux.c',
        'linux/claw_kv_linux.c',
    )
    osal_lib = static_library('osal', osal_src,
        include_directories: [claw_inc] + platform_incs,
        c_args: platform_defs,
        dependencies: [libcurl_dep],
    )
    osal_dep = declare_dependency(
        link_with: osal_lib,
        include_directories: claw_inc,
        dependencies: [libcurl_dep],
    )
else
    # osal='none': OSAL compiled by platform build system.
    osal_dep = declare_dependency(
        include_directories: claw_inc,
    )
endif
