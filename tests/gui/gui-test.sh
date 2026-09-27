#!/bin/sh
# Tests the plug-in and the filter in GIMP's real dialogs, on a Broadway
# display with a headless Chrome, and leaves screenshots in
# tests/output/gui/ (and, with PAS_DOCS=1, the ones for the README in
# docs/):
#
#   plugin  Image > Scale Pixel Art... on the sprite: the dialog, factor
#           set to 4 with its + button, OK; the image is 4 times larger,
#           with no filter left; the result on the canvas at 400%
#   filter  the sprite scaled 4 times with interpolation None, then
#           Filters > Enhance > Pixel Art Rescale... from the menu: the
#           dialog, factor 4, the preview on the canvas, split view, OK;
#           the layer keeps the filter (non-destructive), factor 4
#
# Run tests/gimp-check.sh first (it installs the plug-in into the test
# profile and puts the GEGL operations in place). Needs a headless Chrome
# (google-chrome or chromium), node 22 and ../gimp-devtools (or
# GIMP_PLUGIN_DEVTOOLS) for gui/cdp.mjs. GIMP runs isolated from your
# folders (start.sh); before and after, your folders are listed and the
# test fails if anything there changed. Prints PASS or FAIL for each check
# and exits non-zero if one fails. Stops only the GIMP and the Chrome it
# started.
#
# Copyright 2026 David
# SPDX-License-Identifier: GPL-3.0-or-later

# (pass never fails, so "check && pass || fail" is an if-then-else)
# shellcheck disable=SC2015
here=$(cd "$(dirname "$0")" && pwd)
status=0
pass () { echo "PAS PASS gui $1"; }
fail () { echo "PAS FAIL gui $1"; status=1; }

# shellcheck source=SCRIPTDIR/common.sh
. "$here/common.sh"
# shellcheck source=SCRIPTDIR/../isolate.sh
. "$tests/isolate.sh"
rm -f "$out"/0*.png
snapshot_take "$out/snapshot-before.txt"

# positions, from the dialog's top left corner (the Broadway canvas, with
# the shadow around the window)
PLUGIN_PLUS=185,88
PLUGIN_OK=630,480
FILTER_PLUS=406,167
FILTER_TITLE=223,36
FILTER_SPLIT=321,304
FILTER_OK=372,339
# in the image window (single-window mode, the page's size)
CANVAS=900,760
FILTERS_MENU=427,49
ENHANCE=456,176
RESCALE=723,500

session () {
    name=$1
    PAS_GUI_MODE=$name
    export PAS_GUI_MODE
    start_gimp "$name"
    case $name in
    plugin)
        find_dialog
        if [ -z "$at" ]; then
            fail "plugin: the dialog did not open (log: $out/gimp-$name.log)"
            stop_all
            return
        fi
        pass "plugin: the dialog opened (at $at)"
        plus=$(p ${PLUGIN_PLUS%,*} ${PLUGIN_PLUS#*,})
        $cdp "$view" move:"$(p 300 300)" wait:300 move:"$plus" wait:300 click:"$plus" \
          wait:300 click:"$plus" wait:300 move:"$(p 600 40)" wait:1500 shot:"$out/01-plugin-dialog.png" \
          move:"$(p ${PLUGIN_OK%,*} ${PLUGIN_OK#*,})" wait:300 \
          click:"$(p ${PLUGIN_OK%,*} ${PLUGIN_OK#*,})" >/dev/null
        if ! wait_for "$out/result.txt" 90; then
            fail "plugin: no result (log: $out/gimp-$name.log)"
            stop_all
            return
        fi
        r=$out/result.txt
        grep -q '^status success$' "$r" && pass "plugin: status success" ||
          fail "plugin: $(head -1 "$r")"
        grep -q '^size 112x96$' "$r" && pass "plugin: the image is 4 times larger, 112x96" ||
          fail "plugin: $(grep size "$r")"
        grep -q '^filters 0$' "$r" && pass "plugin: no filter left on the layer" ||
          fail "plugin: $(grep filters "$r")"
        $cdp "$view" wait:1000 click:$CANVAS wait:300 key:3 wait:800 move:1270,600 wait:1000 \
          shot:"$out/02-plugin-result.png" >/dev/null;;
    filter)
        if ! wait_for "$out/ready" 90; then
            fail "filter: GIMP did not get ready (log: $out/gimp-$name.log)"
            stop_all
            return
        fi
        sleep 3
        $cdp "$view" click:$CANVAS wait:300 key:3 wait:1000 click:$FILTERS_MENU wait:1200 \
          move:450,175 wait:400 move:$ENHANCE wait:1500 key:Right wait:1200 \
          shot:"$out/03-filters-menu.png" move:700,500 wait:300 click:$RESCALE wait:4000 \
          >/dev/null
        find_dialog
        if [ -z "$at" ] || [ "$x0" -lt 300 ]; then
            fail "filter: the dialog did not open (log: $out/gimp-$name.log)"
            stop_all
            return
        fi
        pass "filter: the dialog opened from Filters > Enhance (at $at)"
        # factor 4, then the dialog moved right of the image, and split view
        tx=$((x0 + ${FILTER_TITLE%,*})) ty=$((y0 + ${FILTER_TITLE#*,}))
        # (Broadway takes a click in a new window only after the pointer
        # has moved into it)
        plus=$(p ${FILTER_PLUS%,*} ${FILTER_PLUS#*,})
        $cdp "$view" move:"$(p 200 200)" wait:500 move:"$plus" wait:500 click:"$plus" wait:400 \
          click:"$plus" wait:2500 move:$tx,$ty wait:300 down:$tx,$ty \
          move:$((tx + 150)),$ty move:$((tx + 330)),$((ty - 150)) \
          up:$((tx + 331)),$((ty - 150)) wait:1500 shot:"$out/04-filter-dialog.png" >/dev/null
        find_dialog
        split=$(p ${FILTER_SPLIT%,*} ${FILTER_SPLIT#*,})
        ok=$(p ${FILTER_OK%,*} ${FILTER_OK#*,})
        $cdp "$view" move:"$(p 200 200)" wait:300 move:"$split" wait:300 click:"$split" \
          wait:2500 shot:"$out/05-split-view.png" click:"$split" wait:1000 \
          move:"$ok" wait:300 click:"$ok" wait:2500 shot:"$out/06-filter-applied.png" >/dev/null
        touch "$out/check"
        if ! wait_for "$out/result.txt" 60; then
            fail "filter: no result (log: $out/gimp-$name.log)"
            stop_all
            return
        fi
        r=$out/result.txt
        grep -q '^size 112x96$' "$r" && pass "filter: the image keeps its size, 112x96" ||
          fail "filter: $(grep size "$r")"
        grep -q '^filter pixel-art:rescale hqx 4$' "$r" &&
          pass "filter: the layer keeps Pixel Art Rescale, hqx, factor 4 (non-destructive)" ||
          fail "filter: $(grep filter "$r" | tr '\n' ' ')";;
    esac
    touch "$out/done"
    stop_all
}

for s in ${PAS_GUI_ONLY:-plugin filter}; do
    session "$s"
done

# the parts of the screenshots that matter, for the README
if [ "${PAS_DOCS:-0}" = 1 ]; then
    mkdir -p "$src/docs"
    python3 - "$out" "$src/docs" <<'EOF'
import sys
from PIL import Image
out, docs = sys.argv[1:]
for shot, name, box in (('01-plugin-dialog', 'plugin-dialog', (100, 100, 824, 651)),
                        ('03-filters-menu', 'filters-menu', (0, 0, 920, 560)),
                        ('05-split-view', 'filter-split-view', (330, 200, 1270, 700))):
    try:
        Image.open('%s/%s.png' % (out, shot)).crop(box).save('%s/%s.png' % (docs, name))
    except FileNotFoundError:
        pass    # (a session that did not run)
EOF
fi
snapshot_check "$out/snapshot-before.txt" "PAS " || status=1
[ $status = 0 ] && echo "PAS GUI all passed (screenshots in $out)" ||
  echo "PAS GUI FAILED (screenshots in $out)"
exit $status
