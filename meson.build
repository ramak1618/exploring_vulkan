project('exploring_vulkan', 'c', default_options: ['warning_level=3', 'werror=true'], meson_version: '>=1.1' )

add_project_arguments('-DSHADER_DIR="' + meson.current_build_dir() + '/shaders/"', language: 'c')

profile = get_option('profile')

target_options = []
if profile == 'asan'
    target_options = ['buildtype=debug', 'b_sanitize=address']
elif profile == 'debug'
    target_options = ['buildtype=debug']
elif profile == 'opt'
    target_options = ['buildtype=release']
    add_project_arguments('-DDISABLE_VALIDATION', language: 'c')
endif

cc = meson.get_compiler('c')

math = cc.find_library('m')
wl = dependency('wayland-client')
vk = dependency('vulkan')
xkb = dependency('xkbcommon')

subdir('shaders')

executable('v', 'pcg/pcg_basic.c', 'xdg-shell.c', 'v.c', vertex_shader, frag_shader, dependencies: [math, wl, vk, xkb], override_options : target_options)
