project(
  'virtual-ac3-encoder-linux',
  'cpp',
  version : '0.1.0-alpha.1',
  default_options : [
    'cpp_std=c++20',
    'warning_level=2',
    'buildtype=release',
  ],
  license : 'MIT',
)

pipewire_dep = dependency('libpipewire-0.3', version : '>=0.3.50', include_type : 'system')
avcodec_dep = dependency('libavcodec', version : '>=59', include_type : 'system')
avutil_dep = dependency('libavutil', include_type : 'system')
threads_dep = dependency('threads')

pipewire_moduledir = pipewire_dep.get_variable(pkgconfig : 'moduledir')

shared_module(
  'pipewire-module-virtual-ac3-encoder',
  'src/module.cpp',
  'src/ac3_encoder.cpp',
  include_directories : include_directories('src'),
  dependencies : [pipewire_dep, avcodec_dep, avutil_dep, threads_dep],
  install : true,
  install_dir : pipewire_moduledir,
  name_prefix : 'lib',
)

install_data(
  'config/50-virtual-ac3-encoder.conf',
  install_dir : join_paths(get_option('datadir'), 'pipewire/pipewire.conf.d'),
)

subdir('tests')
