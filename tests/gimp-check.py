# Runs inside GIMP 3 without a window (tests/gimp-check.sh starts it): the
# plug-in Image > Scale Pixel Art... and the filter pixel-art:rescale, the
# way they are used in GIMP. Writes PNG files and raw pixel dumps into
# PAS_OUT for tests/gimp-check.sh, which compares them with the gegl
# command line, and prints "PAS PASS ..." or "PAS FAIL ..." for what it
# can check itself (sizes, offsets, guides, paths, filters in XCF).
#
# PAS_PHASE=missing: GIMP runs without the GEGL operations, and the plug-in
# must say so.
#
# Copyright 2026 David
# SPDX-License-Identifier: GPL-3.0-or-later
import json
import os

import gi
gi.require_version('Gimp', '3.0')
gi.require_version('Gegl', '0.4')
from gi.repository import Gimp, Gegl, Gio

out = os.environ['PAS_OUT']
images = os.environ['PAS_IMAGES']
phase = os.environ.get('PAS_PHASE', 'main')
pdb = Gimp.get_pdb()
failures = 0
manifest = {}


def check(ok, what):
    global failures
    print('PAS %s %s' % ('PASS' if ok else 'FAIL', what), flush=True)
    if not ok:
        failures += 1


def load(name):
    return Gimp.file_load(Gimp.RunMode.NONINTERACTIVE,
                          Gio.File.new_for_path(os.path.join(images, name)))


def export(image, name):
    Gimp.file_save(Gimp.RunMode.NONINTERACTIVE, image,
                   Gio.File.new_for_path(os.path.join(out, name)), None)


def dump(drawable, name):
    """The drawable's pixels, raw 8-bit (R'G'B'A, or for a mask its own
    linear Y: GIMP keeps masks linear, and the operation works on those
    values), and its size, for gimp-check.sh"""
    buf = drawable.get_buffer()
    r = buf.get_extent()
    fmt = "Y u8" if isinstance(drawable, Gimp.Channel) else "R'G'B'A u8"
    data = buf.get(r, 1.0, fmt, Gegl.AbyssPolicy.NONE)
    with open(os.path.join(out, name + '.raw'), 'wb') as f:
        f.write(data)
    manifest[name] = [r.width, r.height, fmt]


def scale_plugin(image, algorithm='hqx', factor=2, alpha='weighted', edges='clamp',
                 keep=False):
    proc = pdb.lookup_procedure('plug-in-pixel-art-scale')
    config = proc.create_config()
    config.set_property('run-mode', Gimp.RunMode.NONINTERACTIVE)
    config.set_property('image', image)
    config.set_core_object_array('drawables', image.get_selected_drawables())
    config.set_property('algorithm', algorithm)
    config.set_property('factor', factor)
    config.set_property('alpha', alpha)
    config.set_property('edges', edges)
    config.set_property('keep-filter', keep)
    result = proc.run(config)
    return result.index(0), result


def missing():
    image = load('mushroom.png')
    status, result = scale_plugin(image)
    check(status == Gimp.PDBStatusType.EXECUTION_ERROR and
          image.get_width() == 28,
          'without the GEGL operation the plug-in stops with an error and leaves '
          'the image as it is (status %s)' % status.value_nick)
    print('PAS failures: %d' % failures, flush=True)


def main():
    global failures
    # ---- A: the plug-in on the sprite, every algorithm and factor
    for algorithm in ('hqx', 'xbr', 'scalenx', 'nearest'):
        for n in (2, 3, 4):
            image = load('mushroom.png')
            status, result = scale_plugin(image, algorithm, n)
            ok = (status == Gimp.PDBStatusType.SUCCESS and
                  image.get_width() == 28 * n and image.get_height() == 24 * n)
            layer = image.get_layers()[0]
            ok = ok and layer.get_width() == 28 * n and not layer.get_filters()
            if not ok:
                check(False, 'plug-in %s x%d: status %s, image %dx%d' %
                      (algorithm, n, status.value_nick, image.get_width(), image.get_height()))
            export(image, 'plugin-%s-%d.png' % (algorithm, n))
            image.delete()
    check(failures == 0, 'plug-in on the sprite: every algorithm at x2, x3 and x4 makes the '
          'image and its layer exactly that many times larger, with no filter left')

    # ---- B: an image with several layers, offsets, a group, a mask, a
    # channel, a path and a guide
    image = Gimp.Image.new(40, 30, Gimp.ImageBaseType.RGB)
    blocks = Gimp.file_load_layer(Gimp.RunMode.NONINTERACTIVE, image,
                                  Gio.File.new_for_path(os.path.join(images, 'blocks.png')))
    blocks.set_name('blocks')
    image.insert_layer(blocks, None, 0)
    mush = Gimp.file_load_layer(Gimp.RunMode.NONINTERACTIVE, image,
                                Gio.File.new_for_path(os.path.join(images, 'mushroom.png')))
    mush.set_name('mushroom')
    image.insert_layer(mush, None, 0)
    mush.set_offsets(5, 3)
    mask = mush.create_mask(Gimp.AddMaskType.ALPHA)
    mush.add_mask(mask)
    group = Gimp.GroupLayer.new(image, 'group')
    image.insert_layer(group, None, 0)
    text = Gimp.file_load_layer(Gimp.RunMode.NONINTERACTIVE, image,
                                Gio.File.new_for_path(os.path.join(images, 'text-outline.png')))
    text.set_name('text')
    image.insert_layer(text, group, 0)
    text.set_offsets(-3, 7)
    image.select_rectangle(Gimp.ChannelOps.REPLACE, 4, 5, 10, 6)
    channel = Gimp.Selection.save(image)
    channel.set_name('saved')
    Gimp.Selection.none(image)
    path = Gimp.Path.new(image, 'path')
    image.insert_path(path, None, 0)
    path.stroke_new_from_points(Gimp.PathStrokeType.BEZIER,
                                [2, 3, 2, 3, 2, 3, 12, 5, 12, 5, 12, 5], False)
    guide = image.add_hguide(7)
    for d, name in ((blocks, 'b-blocks-before'), (mush, 'b-mushroom-before'),
                    (mask, 'b-mask-before'), (text, 'b-text-before')):
        dump(d, name)
    image.set_selected_layers([mush])

    status, result = scale_plugin(image, 'xbr', 3)
    check(status == Gimp.PDBStatusType.SUCCESS, 'plug-in on a layered image: success')
    check(image.get_width() == 120 and image.get_height() == 90, 'the canvas is 3 times '
          'larger: %dx%d' % (image.get_width(), image.get_height()))
    sizes = [(l.get_name(), l.get_width(), l.get_height(), tuple(l.get_offsets()[1:]))
             for l in (blocks, mush, text)]
    check(sizes == [('blocks', 120, 90, (0, 0)), ('mushroom', 84, 72, (15, 9)),
                    ('text', 210, 60, (-9, 21))],
          'layers 3 times larger at 3 times their offsets, also in a group: %s' % sizes)
    check(mask.get_width() == 84 and mask.get_height() == 72, 'the layer mask is scaled '
          'with its layer')
    check(channel.get_width() == 120 and channel.get_height() == 90, 'the saved '
          'selection is scaled with the image')
    check(image.get_guide_position(guide) == 21, 'the guide moves from 7 to 21 (%d)'
          % image.get_guide_position(guide))
    points = path.stroke_get_points(path.get_strokes()[0])
    check(list(points[1])[:6] == [6, 9, 6, 9, 6, 9], 'the path is scaled: %s'
          % list(points[1])[:6])
    check(all(not l.get_filters() for l in (blocks, mush, text)), 'no filters are left')
    for d, name in ((blocks, 'b-blocks'), (mush, 'b-mushroom'), (mask, 'b-mask'),
                    (text, 'b-text')):
        dump(d, name)
    image.delete()

    # ---- C: the filter route: Image > Scale Image with interpolation None,
    # then Filters > Enhance > Pixel Art Rescale, non-destructive
    image = load('mushroom.png')
    Gimp.context_push()
    Gimp.context_set_interpolation(Gimp.InterpolationType.NONE)
    image.scale(28 * 3, 24 * 3)
    Gimp.context_pop()
    layer = image.get_layers()[0]
    dump(layer, 'c-nearest')
    f = Gimp.DrawableFilter.new(layer, 'pixel-art:rescale', 'Pixel Art Rescale')
    config = f.get_config()
    config.set_property('algorithm', 'xbr')
    config.set_property('factor', 3)
    f.update()
    layer.append_filter(f)
    check(layer.get_width() == 84 and len(layer.get_filters()) == 1, 'the filter on the '
          'scaled layer keeps its size and stays on it')
    export(image, 'filter-xbr-3.png')
    xcf = os.path.join(out, 'filter.xcf')
    Gimp.file_save(Gimp.RunMode.NONINTERACTIVE, image, Gio.File.new_for_path(xcf), None)
    image.delete()
    image = Gimp.file_load(Gimp.RunMode.NONINTERACTIVE, Gio.File.new_for_path(xcf))
    layer = image.get_layers()[0]
    filters = layer.get_filters()
    ok = len(filters) == 1 and filters[0].get_operation_name() == 'pixel-art:rescale'
    if ok:
        c = filters[0].get_config()
        ok = c.get_property('algorithm') == 'xbr' and c.get_property('factor') == 3
    check(ok, 'the filter and its settings come back from the XCF file')
    export(image, 'filter-xbr-3-xcf.png')
    # changing it later, as in the layer's effects
    if ok:
        c.set_property('algorithm', 'scalenx')
        filters[0].update()
        export(image, 'filter-scalenx-3.png')
    image.delete()

    # ---- D: the plug-in, keeping the filter
    image = load('mushroom.png')
    status, result = scale_plugin(image, 'hqx', 4, keep=True)
    layer = image.get_layers()[0]
    filters = layer.get_filters()
    check(status == Gimp.PDBStatusType.SUCCESS and len(filters) == 1 and
          filters[0].get_operation_name() == 'pixel-art:rescale' and
          filters[0].get_config().get_property('factor') == 4,
          'plug-in with "keep the filter": the layer has Pixel Art Rescale, factor 4')
    export(image, 'plugin-keep-hqx-4.png')
    image.delete()

    # ---- E: a grayscale image
    image = load('mushroom.png')
    image.convert_grayscale()
    dump(image.get_layers()[0], 'e-gray-before')
    status, result = scale_plugin(image, 'hqx', 2)
    check(status == Gimp.PDBStatusType.SUCCESS and image.get_width() == 56,
          'plug-in on a grayscale image')
    dump(image.get_layers()[0], 'e-gray')
    image.delete()

    with open(os.path.join(out, 'manifest.json'), 'w') as f:
        json.dump(manifest, f)
    print('PAS failures: %d' % failures, flush=True)


if phase == 'missing':
    missing()
else:
    main()
