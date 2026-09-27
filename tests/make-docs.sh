#!/bin/sh
# Makes the pictures of the README from the test sprite (tests/sprites.c,
# drawn for this repository): docs/comparison.png, the sprite scaled 4
# times with every algorithm, and docs/alpha.png, hqx with alpha weighted
# and straight over a colored background. The scaling is done by the
# operation on the gegl command line, in the Flatpak GIMP's GEGL, isolated
# from your folders like the other tests. Needs python3 with PIL.
#
# Copyright 2026 David
# SPDX-License-Identifier: GPL-3.0-or-later
set -e

here=$(cd "$(dirname "$0")" && pwd)
top=$(dirname "$here")
out="$here/output"
d="$out/docs"
mkdir -p "$d" "$top/docs"
# (isolate.sh needs $src)
# shellcheck disable=SC2034
src=$top
GIMP_RUN_HOME=${GIMP_RUN_HOME:-$out/gimp-home}
export GIMP_RUN_HOME
# shellcheck source=SCRIPTDIR/isolate.sh
. "$here/isolate.sh"

gimp_build=${GIMP_BUILD:-$top/../gimp-devtools/gimp-build.sh}
in_sdk () { "$gimp_build" "$top" "$*"; }
if [ ! -f "$out/build-check/build.ninja" ]; then
  in_sdk meson setup tests/output/build-check -Dwarning_level=2 >/dev/null
fi
in_sdk ninja -C tests/output/build-check >/dev/null
mkdir -p "$out/images" "$out/docs-modules"
cp "$out/build-check/pixel-art-scale.so" "$out/docs-modules/"
in_sdk "tests/output/build-check/sprites tests/output/build-check/pixel-art-scale.so tests/output/images" >/dev/null

# shellcheck disable=SC2016
gimp_run --flatpak --filesystem="$top" --env=GEGL_PATH="$out/docs-modules:/app/lib/gegl-0.4" -- sh -c '
  i=$1 d=$2
  for a in nearest scalenx hqx xbr; do
    gegl "$i/mushroom.png" -o "$d/$a.png" -- pixel-art:scale algorithm=$a factor=4
  done
  for m in weighted straight; do
    gegl "$i/mushroom.png" -o "$d/alpha-$m.png" -- pixel-art:scale algorithm=hqx factor=4 alpha=$m
  done' sh "$out/images" "$d"

python3 - "$out/images" "$d" "$top/docs" <<'PY'
import sys
from PIL import Image, ImageDraw, ImageFont
images, d, docs = sys.argv[1:]
bg, fg, panel = (238, 234, 222), (40, 40, 52), (170, 205, 235)
try:
    font = ImageFont.truetype('DejaVuSans.ttf', 17)
except OSError:
    font = ImageFont.load_default()

def on_panel(im):
    p = Image.new('RGBA', im.size, panel + (255,))
    p.alpha_composite(im.convert('RGBA'))
    return p

orig = Image.open(images + '/mushroom.png').convert('RGBA')
tiles = [('Original', orig),
         ('Nearest x4', Image.open(d + '/nearest.png')),
         ('Scale4x (EPX)', Image.open(d + '/scalenx.png')),
         ('hq4x', Image.open(d + '/hqx.png')),
         ('xBR 4x', Image.open(d + '/xbr.png'))]
# every tile shown twice as large (nearest), so that its pixels show
z, w, h = 2, 112 * 2, 96 * 2
pad, label = 20, 34
out = Image.new('RGB', (pad + len(tiles) * (w + pad), label + h + 2 * pad), bg)
draw = ImageDraw.Draw(out)
for i, (name, im) in enumerate(tiles):
    x = pad + i * (w + pad)
    t = on_panel(im)
    t = t.resize((t.width * z, t.height * z), Image.NEAREST)
    out.paste(t, (x + (w - t.width) // 2, label + pad + (h - t.height) // 2))
    draw.text((x + w // 2, pad), name, fill=fg, font=font, anchor='mt')
out.save(docs + '/comparison.png')

# hqx: weighted and straight, a part of the outline at 3x more (nearest)
crop = (0, 0, 64, 48)
tiles = [('alpha weighted (default)', Image.open(d + '/alpha-weighted.png')),
         ('alpha straight (as the references)', Image.open(d + '/alpha-straight.png'))]
z = 4
out = Image.new('RGB', (pad + 2 * (64 * z + pad), label + 48 * z + 2 * pad), bg)
draw = ImageDraw.Draw(out)
for i, (name, im) in enumerate(tiles):
    x = pad + i * (64 * z + pad)
    t = on_panel(im).crop(crop).resize((64 * z, 48 * z), Image.NEAREST)
    out.paste(t, (x, label + pad))
    draw.text((x + 32 * z, pad), name, fill=fg, font=font, anchor='mt')
out.save(docs + '/alpha.png')
print(docs + '/comparison.png')
print(docs + '/alpha.png')
PY
