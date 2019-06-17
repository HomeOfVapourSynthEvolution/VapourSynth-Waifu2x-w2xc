project('Waifu2x-w2xc', 'cpp',
  default_options : ['buildtype=release', 'b_ndebug=if-release', 'cpp_std=c++14'],
  meson_version : '>=0.48.0',
  version : '8'
)

add_project_arguments('-ffast-math', language : 'cpp')

sources = [
  'Waifu2x-w2xc/Waifu2x-w2xc.cpp'
]

cc = meson.get_compiler('cpp')

vapoursynth_dep = dependency('vapoursynth').partial_dependency(compile_args : true, includes : true)

deps = [vapoursynth_dep]

deps += cc.find_library('w2xc')

if host_machine.cpu_family().startswith('x86')
  add_project_arguments('-mfpmath=sse', '-msse2', language : 'cpp')
endif

vapoursynth_plugindir = join_paths(vapoursynth_dep.get_pkgconfig_variable('libdir'), 'vapoursynth')

shared_module('waifu2x-w2xc', sources,
  dependencies : deps,
  install : true,
  install_dir : vapoursynth_plugindir,
  gnu_symbol_visibility : 'hidden'
)

install_subdir('Waifu2x-w2xc/models',
  install_dir : vapoursynth_plugindir
)
