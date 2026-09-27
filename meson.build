project('gegl-pixel-scale', 'c',
  version: '0.1',
  license: 'GPL-3.0-or-later',
  meson_version: '>= 0.60.0',
  default_options: ['warning_level=1', 'buildtype=debugoptimized'],
)

gegl_dep = dependency('gegl-0.4', version: '>= 0.4.62')

# GEGL loads user operations from $XDG_DATA_HOME/gegl-0.4/plug-ins, which is
# ~/.local/share/gegl-0.4/plug-ins, or for the Flatpak version of GIMP
# ~/.var/app/org.gimp.GIMP/data/gegl-0.4/plug-ins.
moduledir = get_option('moduledir')
if moduledir == ''
  moduledir = gegl_dep.get_variable(pkgconfig: 'pluginsdir')
endif

# hqx converts colors to YUV in double precision, exactly as its reference
# does: no fused multiply-adds, whatever -march says
cc = meson.get_compiler('c')
op_args = ['-DGETTEXT_PACKAGE="gegl-pixel-scale"']
op_args += cc.get_supported_arguments('-ffp-contract=off')

scalers = files('operations/scalenx.c', 'operations/hqx.c', 'operations/xbr.c')

pixel_art_scale = shared_module('pixel-art-scale',
  ['operations/pixel-art-scale.c', scalers],
  name_prefix: '',
  include_directories: 'operations',
  dependencies: [gegl_dep],
  c_args: op_args,
  install: true,
  install_dir: moduledir,
)

# numerical checks of the operation: tests/check.sh, or meson test -C build
math_dep = cc.find_library('m', required: false)
check = executable('check', 'tests/check.c',
  dependencies: [gegl_dep, math_dep],
  c_args: ['-DGEGL_PLUGINSDIR="@0@"'.format(
    gegl_dep.get_variable(pkgconfig: 'pluginsdir'))],
  install: false,
)
test('pixel-art-scale', check, args: [pixel_art_scale], timeout: 600)

# how long it takes on a 1024 x 1024 sprite sheet: tests/bench.sh
executable('bench', 'tests/bench.c',
  dependencies: [gegl_dep, math_dep],
  c_args: ['-DGEGL_PLUGINSDIR="@0@"'.format(
    gegl_dep.get_variable(pkgconfig: 'pluginsdir'))],
  install: false,
)
