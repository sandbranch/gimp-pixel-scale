project('gimp-pixel-scale', 'c',
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
op_args = ['-DGETTEXT_PACKAGE="gimp-pixel-scale"']
op_args += cc.get_supported_arguments('-ffp-contract=off')

scalers = files('operations/scalenx.c', 'operations/hqx.c', 'operations/xbr.c')

# each operation is a module of its own; both include operations/pas-gegl.c
modules = {}
foreach op : ['pixel-art-scale', 'pixel-art-rescale']
  modules += { op: shared_module(op,
    ['operations' / op + '.c', scalers],
    name_prefix: '',
    include_directories: 'operations',
    dependencies: [gegl_dep],
    c_args: op_args,
    install: true,
    install_dir: moduledir,
  )}
endforeach
pixel_art_scale = modules['pixel-art-scale']

# numerical checks of the operation: tests/check.sh, or meson test -C build
math_dep = cc.find_library('m', required: false)
check = executable('check', 'tests/check.c',
  dependencies: [gegl_dep, math_dep],
  c_args: ['-DGEGL_PLUGINSDIR="@0@"'.format(
    gegl_dep.get_variable(pkgconfig: 'pluginsdir'))],
  install: false,
)
test('pixel-art-scale', check,
  args: [modules['pixel-art-scale'], modules['pixel-art-rescale']],
  timeout: 600)

# how long it takes on a 1024 x 1024 sprite sheet: tests/bench.sh
executable('bench', 'tests/bench.c',
  dependencies: [gegl_dep, math_dep],
  c_args: ['-DGEGL_PLUGINSDIR="@0@"'.format(
    gegl_dep.get_variable(pkgconfig: 'pluginsdir'))],
  install: false,
)

# the test images as PNG files, for the tests in GIMP: tests/gimp-check.sh
executable('sprites', 'tests/sprites.c',
  dependencies: [gegl_dep, math_dep],
  c_args: ['-DGEGL_PLUGINSDIR="@0@"'.format(
    gegl_dep.get_variable(pkgconfig: 'pluginsdir'))],
  install: false,
)

# the GIMP plug-in, Image > Scale Pixel Art... (Python, nothing to build),
# into <plugindir>/pixel-art-scale/
plugindir = get_option('plugindir')
if plugindir != ''
  install_data('plug-in/pixel-art-scale.py',
    install_dir: plugindir / 'pixel-art-scale',
    install_mode: 'rwxr-xr-x',
  )
endif
