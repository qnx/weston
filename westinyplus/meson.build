project('westinyplus',
  'c', 'cpp',
  version: '0.0.90',
  default_options: [
    'warning_level=3',
    'cpp_std=c++20',
    'werror=true',
  ],
  meson_version: '>= 1.0.0',
  license: 'MIT/Expat',
)

cxx = meson.get_compiler('cpp')

add_project_arguments(
  cxx.get_supported_arguments([
    '-Wno-unused-parameter',
    '-Wno-pedantic',
    '-Wno-deprecated-declarations'
  ]),
  language: 'cpp'
)

add_project_arguments([
    '-DPACKAGE_STRING="westinyplus @0@"'.format(meson.project_version()),
    '-D_GNU_SOURCE',
    '-D_ALL_SOURCE',
  ],
  language: [ 'cpp', 'c' ],
)

exe_westinyplus = executable(
    'westinyplus',
    sources: [ 'src/main.cpp' ],
    dependencies: [ dependency('wayland-server'),
    dependency('weston'),
    dependency('libweston-14') ],
    include_directories : include_directories('.'),
    install: false
)
