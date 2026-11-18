#!/bin/sh
# Tests the plug-in Image > Scale Pixel Art... and the filter
# pixel-art:rescale in the native GIMP, without a window
# (tests/gimp-check.py), and compares what GIMP made with the gegl command
# line (tests/gegl-op.py: pixel-art:scale on the original pixels), byte for byte. Fully
# transparent pixels count as equal whatever color they hide (GIMP's PNG
# export drops those colors).
#
# GIMP runs with a profile of its own in tests/output (the plug-in is
# copied there) and loads the GEGL operations from tests/output/build-check
# through GEGL_PATH, so nothing is installed. GEGL and GIMP run isolated
# from your folders (tests/isolate.sh, with gimp-devtools/gimp-run.sh
# if it is there): HOME and the XDG folders point
# into tests/output/gimp-home. Before and after, it lists your folders of
# GIMP and the other apps (gimp-devtools/snapshot.sh) and fails if
# anything there changed. GIMP loads no fonts (--no-fonts): GIMP 3.2 can
# hang while loading them.
#
# Needs python3 with numpy and PIL. Exits with 1 if anything failed.
#
# Copyright 2026 David
# SPDX-License-Identifier: GPL-3.0-or-later
set -e

here=$(cd "$(dirname "$0")" && pwd)
top=$(dirname "$here")
out="$here/output"
g="$out/gimp"
mkdir -p "$out"
# (isolate.sh needs $src)
# shellcheck disable=SC2034
src=$top
GIMP_RUN_HOME=${GIMP_RUN_HOME:-$out/gimp-home}
export GIMP_RUN_HOME
# shellcheck source=SCRIPTDIR/isolate.sh
. "$here/isolate.sh"
snapshot_take "$out/snapshot-gimp-before.txt"

gimp_build=${GIMP_BUILD:-$top/../gimp-devtools/gimp-build.sh}
in_sdk () { "$gimp_build" "$top" "$*"; }
if [ ! -f "$out/build-check/build.ninja" ]; then
  in_sdk meson setup tests/output/build-check -Dwarning_level=2 >/dev/null
fi
in_sdk ninja -C tests/output/build-check >/dev/null

# a folder with only the modules (GEGL loads every file of a folder), the
# test images, a fresh profile with the plug-in
mod="$out/gimp-modules"
rm -rf "$mod" "$g" "$out/gimp-profile"
mkdir -p "$mod" "$g" "$out/images" "$out/gimp-profile/plug-ins/pixel-art-scale"
cp "$out/build-check/pixel-art-scale.so" "$out/build-check/pixel-art-rescale.so" "$mod/"
in_sdk "tests/output/build-check/sprites tests/output/build-check/pixel-art-scale.so tests/output/images" >/dev/null
cp "$top/plug-in/pixel-art-scale.py" "$out/gimp-profile/plug-ins/pixel-art-scale/"
chmod 755 "$out/gimp-profile/plug-ins/pixel-art-scale/pixel-art-scale.py"

status=0
run_gimp () {
  phase=$1 path=$2
  gimp_run --timeout=900 \
    --env=PAS_OUT="$g" --env=PAS_IMAGES="$out/images" --env=PAS_PHASE="$phase" \
    --env=GIMP3_DIRECTORY="$out/gimp-profile" \
    --env=GEGL_PATH="$path" \
    -- gimp-console-3.2 --new-instance --no-interface --no-data --no-fonts \
    --batch-interpreter python-fu-eval \
    -b "exec(open('$here/gimp-check.py').read())" --quit >"$g/gimp-$phase.log" 2>&1 || true
  grep -E '^PAS|Traceback|^  File|Error' "$g/gimp-$phase.log" || true
  grep -q '^PAS failures: 0$' "$g/gimp-$phase.log" || status=1
  if grep -E 'pixel-art-scale.py.*(WARNING|CRITICAL)|pixel-art-scale.py.*Traceback' \
       "$g/gimp-$phase.log"; then
    echo "PAS FAIL warnings or tracebacks from the plug-in, see $g/gimp-$phase.log"
    status=1
  fi
}

gegl_ops=$(pkg-config --variable=pluginsdir gegl-0.4)
echo "== GIMP"
run_gimp main "$mod:$gegl_ops"
echo "== GIMP without the GEGL operations"
run_gimp missing "$gegl_ops"

# the expected results, from the gegl command line: raw dumps become PNG
# files first
echo "== against the gegl command line"
python3 - "$g" <<'EOF'
import json, sys
import numpy as np
from PIL import Image
g = sys.argv[1]
m = json.load(open(g + '/manifest.json'))
for name, (w, h, fmt) in m.items():
    a = np.fromfile('%s/%s.raw' % (g, name), np.uint8)
    mode = 'L' if fmt.startswith('Y') else 'RGBA'
    Image.fromarray(a.reshape(h, w, -1 if mode == 'RGBA' else 1).squeeze(), mode) \
        .save('%s/%s.png' % (g, name))
EOF
jobs=""
for a in hqx xbr scalenx nearest; do
  for n in 2 3 4; do
    jobs="$jobs $out/images/mushroom.png:exp-$a-$n.png:$a:$n"
  done
done
jobs="$jobs $g/b-blocks-before.png:exp-b-blocks.png:xbr:3 $g/b-mushroom-before.png:exp-b-mushroom.png:xbr:3"
jobs="$jobs $g/b-mask-before.png:exp-b-mask.png:xbr:3 $g/b-text-before.png:exp-b-text.png:xbr:3"
jobs="$jobs $g/e-gray-before.png:exp-e-gray.png:hqx:2"
# (the jobs are words without spaces: split on purpose)
# shellcheck disable=SC2016,SC2086
gimp_run --env=GEGL_PATH="$mod:$gegl_ops" -- sh -c '
  op=$1 g=$2; shift 2
  for j; do
    IFS=: read -r in exp a n <<EOT
$j
EOT
    python3 "$op" "$in" "$g/$exp" pixel-art:scale algorithm=$a factor=$n || echo "gegl failed: $j"
  done' sh "$here/gegl-op.py" "$g" $jobs

python3 - "$g" <<'EOF' || status=1
import sys
import numpy as np
from PIL import Image
g = sys.argv[1]
failures = 0

def rgba(path):
    return np.asarray(Image.open(path).convert('RGBA')).astype(int)

def same(got, want):
    """equal, fully transparent pixels whatever they hide"""
    if got.shape != want.shape:
        return False, 'size %s, expected %s' % (got.shape[:2], want.shape[:2])
    clear = (got[..., 3] == 0) & (want[..., 3] == 0)
    d = np.abs(got - want).max(axis=-1)
    d[clear] = 0
    bad = int((d > 0).sum())
    return bad == 0, '%d of %d pixels differ' % (bad, d.size)

def check(ok, what):
    global failures
    print('PAS %s %s' % ('PASS' if ok else 'FAIL', what))
    failures += not ok

n_ok = 0
for a in ('hqx', 'xbr', 'scalenx', 'nearest'):
    for n in (2, 3, 4):
        ok, why = same(rgba('%s/plugin-%s-%d.png' % (g, a, n)), rgba('%s/exp-%s-%d.png' % (g, a, n)))
        if not ok:
            check(False, 'plug-in %s x%d = gegl pixel-art:scale: %s' % (a, n, why))
        n_ok += ok
check(n_ok == 12, 'plug-in = gegl pixel-art:scale, byte for byte, for %d of 12 '
      '(4 algorithms, x2 to x4)' % n_ok)
for name in ('b-blocks', 'b-mushroom', 'b-text'):
    ok, why = same(rgba('%s/%s.png' % (g, name)), rgba('%s/exp-%s.png' % (g, name)))
    check(ok, 'layered image, layer %s = gegl pixel-art:scale of its pixels: %s'
          % (name[2:], why))
mask = np.asarray(Image.open(g + '/b-mask.png')).astype(int)
want = rgba(g + '/exp-b-mask.png')[..., 0]
check(mask.shape == want.shape and (mask == want).all(),
      'layered image, the layer mask = gegl pixel-art:scale of the mask')
near = rgba(g + '/c-nearest.png')
orig = rgba(g.rsplit('/', 1)[0] + '/images/mushroom.png')
big = orig.repeat(3, axis=0).repeat(3, axis=1)
ok, why = same(near, big)
check(ok, "GIMP's Image > Scale Image with interpolation None at x3 repeats "
      'every pixel exactly 3 x 3 times: %s' % why)
for got, exp, what in (('filter-xbr-3', 'exp-xbr-3', 'the filter route (scale with None, '
                        'then Pixel Art Rescale, non-destructive)'),
                       ('filter-xbr-3-xcf', 'exp-xbr-3', 'the same after saving and loading '
                        'the XCF'),
                       ('filter-scalenx-3', 'exp-scalenx-3', 'the filter changed to Scale3x '
                        'afterwards'),
                       ('plugin-keep-hqx-4', 'exp-hqx-4', 'the plug-in keeping the filter')):
    ok, why = same(rgba('%s/%s.png' % (g, got)), rgba('%s/%s.png' % (g, exp)))
    check(ok, '%s = gegl pixel-art:scale: %s' % (what, why))
gray = np.asarray(Image.open(g + '/e-gray.png').convert('RGBA')).astype(int)
ok, why = same(gray, rgba(g + '/exp-e-gray.png'))
check(ok, 'grayscale image = gegl pixel-art:scale: %s' % why)
print('PAS failures: %d' % failures)
sys.exit(1 if failures else 0)
EOF

snapshot_check "$out/snapshot-gimp-before.txt" "PAS " || status=1
exit $status
