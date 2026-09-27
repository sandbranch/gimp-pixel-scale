# Runs inside GIMP on a Broadway display (tests/gui/start.sh).
#
# PAS_GUI_MODE=plugin: opens the test sprite and runs Image > Scale Pixel
# Art... with its dialog. When the dialog is closed, writes the status and
# the image size to PAS_OUT/result.txt, waits for PAS_OUT/done (the test
# takes its screenshots meanwhile) and quits GIMP.
#
# PAS_GUI_MODE=filter: opens the sprite scaled up 4 times with
# interpolation None, writes PAS_OUT/ready, and waits for PAS_OUT/check:
# meanwhile the test opens Filters > Enhance > Pixel Art Rescale from the
# action search and applies it. Then writes the layer's filters to
# result.txt, waits for done and quits.
#
# Copyright 2026 David
# SPDX-License-Identifier: GPL-3.0-or-later
import os
import time

import gi
gi.require_version('Gimp', '3.0')
from gi.repository import Gimp, Gio

out = os.environ['PAS_OUT']
mode = os.environ.get('PAS_GUI_MODE', 'plugin')
pdb = Gimp.get_pdb()


def write(name, lines):
    with open(os.path.join(out, name + '.tmp'), 'w') as f:
        f.write('\n'.join(lines) + '\n')
    os.rename(os.path.join(out, name + '.tmp'), os.path.join(out, name))


def wait(name, seconds=120):
    t = time.time()
    while not os.path.exists(os.path.join(out, name)) and time.time() - t < seconds:
        time.sleep(0.5)


image = Gimp.file_load(Gimp.RunMode.NONINTERACTIVE, Gio.File.new_for_path(
    os.path.join(os.environ['PAS_IMAGES'], 'mushroom.png')))
if mode == 'filter':
    Gimp.context_push()
    Gimp.context_set_interpolation(Gimp.InterpolationType.NONE)
    image.scale(image.get_width() * 4, image.get_height() * 4)
    Gimp.context_pop()
image.clean_all()
display = Gimp.Display.new(image)
Gimp.displays_flush()

if mode == 'plugin':
    proc = pdb.lookup_procedure('plug-in-pixel-art-scale')
    config = proc.create_config()
    config.set_property('run-mode', Gimp.RunMode.INTERACTIVE)
    config.set_property('image', image)
    config.set_core_object_array('drawables', image.get_selected_drawables())
    result = proc.run(config)
    status = result.index(0)
    layer = image.get_layers()[0]
    write('result.txt', ['status %s' % status.value_nick,
                         'size %dx%d' % (image.get_width(), image.get_height()),
                         'filters %d' % len(layer.get_filters())])
else:
    write('ready', ['ready'])
    wait('check')
    layer = image.get_layers()[0]
    lines = ['size %dx%d' % (image.get_width(), image.get_height())]
    for f in layer.get_filters():
        c = f.get_config()
        lines.append('filter %s %s %d' % (f.get_operation_name(),
                                          c.get_property('algorithm'),
                                          c.get_property('factor')))
    write('result.txt', lines)

wait('done')
quit_proc = pdb.lookup_procedure('gimp-quit')
quit_config = quit_proc.create_config()
quit_config.set_property('force', True)
quit_proc.run(quit_config)
