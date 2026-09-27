#!/usr/bin/env python3
# -*- coding: utf-8 -*-
#
# Scale Pixel Art for GIMP 3
# Copyright 2026 David
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <https://www.gnu.org/licenses/>.
#
# SPDX-License-Identifier: GPL-3.0-or-later

"""Scale Pixel Art for GIMP 3: Image > Scale Pixel Art...

Scales the whole image 2, 3 or 4 times with a pixel art scaler (hqx, xBR,
Scale2x/3x/4x) in one undo step: first as Image > Scale Image does with
interpolation None, so that the canvas, every layer and its offsets, the
layer masks, channels, the selection, paths and guides grow exactly n
times; then every layer and layer mask gets the GEGL filter
pixel-art:rescale, which redraws the blocks of n x n pixels with the
scaler. The filter can stay on the layers, non-destructively, to be
changed later in the layer's effects.

Needs the GEGL operations of this repository (pixel-art-rescale.so) in
GEGL's folder of user operations; see README.md.
"""

import sys

import gi
gi.require_version('Gimp', '3.0')
from gi.repository import Gimp, GLib, GObject

PROC = 'plug-in-pixel-art-scale'
OP = 'pixel-art:rescale'
PLUG_IN_BINARY = 'pixel-art-scale'
AUTHOR = 'David'
COPYRIGHT = 'David, GPL-3.0-or-later'
DATE = '2026'

ALGORITHMS = [
    ('hqx', 'hq_x (Maxim Stepin): smooth blends'),
    ('xbr', 'x_BR (Hyllian): smooth, sharper edges'),
    ('scalenx', '_Scale2x, Scale3x, Scale4x (EPX): only the image\'s own colors'),
    ('nearest', '_Nearest neighbour: square blocks'),
]
ALPHA = [
    ('weighted', 'Weighted by alpha: hidden colors do not bleed'),
    ('straight', 'Like a color, as the reference implementations'),
]
EDGES = [
    ('clamp', 'Repeat the edge pixels'),
    ('wrap', 'Wrap around, for tiles that repeat'),
]


def make_choice(table):
    choice = Gimp.Choice.new()
    for i, (nick, label) in enumerate(table):
        choice.add(nick, i, label, '')
    return choice


def all_drawables(layers):
    """The layers (not the groups, which follow their children) and the
    layer masks, of layers and of their groups, depth first."""
    for layer in layers:
        if layer.is_group_layer():
            yield from all_drawables(layer.get_children())
        else:
            yield layer
        mask = layer.get_mask()
        if mask is not None:
            yield mask


def fail(procedure, message):
    return procedure.new_return_values(Gimp.PDBStatusType.EXECUTION_ERROR,
                                       GLib.Error(message))


def scale_pixel_art(image, algorithm, factor, alpha, edges, keep):
    """Scales the image; returns the number of layers and masks filtered."""
    width, height = image.get_width(), image.get_height()
    image.undo_group_start()
    try:
        Gimp.context_push()
        try:
            Gimp.context_set_interpolation(Gimp.InterpolationType.NONE)
            image.scale(width * factor, height * factor)
        finally:
            Gimp.context_pop()
        count = 0
        for drawable in all_drawables(image.get_layers()):
            f = Gimp.DrawableFilter.new(drawable, OP, 'Pixel Art Rescale')
            config = f.get_config()
            config.set_property('algorithm', algorithm)
            config.set_property('factor', factor)
            config.set_property('alpha', alpha)
            config.set_property('edges', edges)
            f.update()
            # layer masks always take the result: their filters would not
            # show in the layer's effects
            if keep and isinstance(drawable, Gimp.Layer):
                drawable.append_filter(f)
            else:
                drawable.merge_filter(f)
            count += 1
    finally:
        image.undo_group_end()
    return count


def show_dialog(procedure, config):
    gi.require_version('GimpUi', '3.0')
    from gi.repository import GimpUi
    GimpUi.init(PLUG_IN_BINARY)
    dialog = GimpUi.ProcedureDialog.new(procedure, config, 'Scale Pixel Art')
    for name in ('algorithm', 'alpha', 'edges'):
        dialog.get_widget(name, GimpUi.IntRadioFrame)
    dialog.get_label('note', 'The image, all its layers and masks become this many '
                     'times larger. Undo takes it back in one step.', False, False)
    dialog.fill(['factor', 'algorithm', 'alpha', 'edges', 'keep-filter', 'note'])
    try:
        return dialog.run()
    finally:
        dialog.destroy()


def run(procedure, run_mode, image, drawables, config, data):
    if OP not in Gimp.DrawableFilter.operation_get_available():
        return fail(procedure, 'The GEGL operation %s is not installed: put '
                    'pixel-art-rescale.so into the folder of GEGL operations '
                    '(see the README of gegl-pixel-scale) and restart GIMP.' % OP)
    if run_mode == Gimp.RunMode.INTERACTIVE and not show_dialog(procedure, config):
        return procedure.new_return_values(Gimp.PDBStatusType.CANCEL, GLib.Error())
    factor = config.get_property('factor')
    if image.get_width() * factor > Gimp.MAX_IMAGE_SIZE or \
       image.get_height() * factor > Gimp.MAX_IMAGE_SIZE:
        return fail(procedure, 'The image would be larger than %d pixels.'
                    % Gimp.MAX_IMAGE_SIZE)
    scale_pixel_art(image, config.get_property('algorithm'), factor,
                    config.get_property('alpha'), config.get_property('edges'),
                    config.get_property('keep-filter'))
    Gimp.displays_flush()
    return procedure.new_return_values(Gimp.PDBStatusType.SUCCESS, GLib.Error())


class PixelArtScale(Gimp.PlugIn):

    def do_set_i18n(self, name):
        return False

    def do_query_procedures(self):
        return [PROC]

    def do_create_procedure(self, name):
        flags = GObject.ParamFlags.READWRITE
        procedure = Gimp.ImageProcedure.new(self, name, Gimp.PDBProcType.PLUGIN, run, None)
        procedure.set_menu_label('Scale _Pixel Art...')
        procedure.set_documentation(
            'Scale the image up 2, 3 or 4 times with a pixel art scaler.',
            'Scales the image, its layers, masks, channels, paths and guides up 2, 3 or '
            '4 times as Image > Scale Image does with interpolation None, then redraws '
            'every layer and layer mask with hqx, xBR, Scale2x/3x/4x or nearest '
            'neighbour (the GEGL filter pixel-art:rescale), in one undo step. The '
            'filter can stay on the layers, non-destructively.', name)
        procedure.add_int_argument('factor', '_Factor', 'How many times larger: 2, 3 or 4',
                                   2, 4, 2, flags)
        procedure.add_choice_argument('algorithm', 'Algorithm', 'The pixel art scaler',
                                      make_choice(ALGORITHMS), 'hqx', flags)
        procedure.add_choice_argument('alpha', 'Transparency',
                                      'How alpha enters the comparisons and blends',
                                      make_choice(ALPHA), 'weighted', flags)
        procedure.add_choice_argument('edges', 'Edges', 'What lies beyond the edges',
                                      make_choice(EDGES), 'clamp', flags)
        procedure.add_boolean_argument('keep-filter', '_Keep the filter on the layers',
                                       'Leave Pixel Art Rescale on each layer as a '
                                       'non-destructive filter (changeable later in the '
                                       'layer\'s effects) instead of applying it',
                                       False, flags)
        procedure.set_image_types('RGB*, GRAY*')
        procedure.set_sensitivity_mask(Gimp.ProcedureSensitivityMask.DRAWABLE |
                                       Gimp.ProcedureSensitivityMask.DRAWABLES |
                                       Gimp.ProcedureSensitivityMask.NO_DRAWABLES)
        procedure.set_attribution(AUTHOR, COPYRIGHT, DATE)
        procedure.add_menu_path('<Image>/Image')
        return procedure


if __name__ == '__main__':
    Gimp.main(PixelArtScale.__gtype__, sys.argv)
