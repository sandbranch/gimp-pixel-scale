#!/bin/sh
# Starts the native GIMP on a Broadway display, on the port
# PAS_BROADWAY_PORT (the page is http://127.0.0.1:port/), with
# tests/gui/gui-script.py, in the profile of tests/gimp-check.sh (which
# installs the plug-in there) and the GEGL operations of
# tests/output/gimp-modules. Isolated from your folders like the other
# tests (tests/isolate.sh). broadwayd stops when GIMP quits. GIMP starts
# as a new instance (--new-instance), so that it never hands its script to
# another GIMP that is running, and loads no fonts (--no-fonts).
#
# GIMP starts when tests/output/gui/page-open exists: GIMP places its
# windows for the size of the Broadway screen, which is that of the page
# once a browser shows it (common.sh does so first).
#
# Copyright 2026 David
# SPDX-License-Identifier: GPL-3.0-or-later
here=$(cd "$(dirname "$0")" && pwd)
tests=$(dirname "$here")
src=$(dirname "$tests")
out=$tests/output/gui
profile=$tests/output/gimp-profile
port=${PAS_BROADWAY_PORT:-8088}
display=$((port - 8080))
GIMP_RUN_HOME=$tests/output/gui-home
export GIMP_RUN_HOME
# shellcheck source=SCRIPTDIR/../isolate.sh
. "$tests/isolate.sh"
mkdir -p "$out"
# no Welcome dialog: GIMP shows it when the profile is of an older
# version, or when asked to
if ! grep -q config-version "$profile/gimprc" 2>/dev/null; then
    version=$(gimp-console-3.2 --version | sed 's/.* //')
    printf '(config-version "%s")\n' "$version" >> "$profile/gimprc"
fi
grep -q show-welcome-dialog "$profile/gimprc" 2>/dev/null ||
  echo '(show-welcome-dialog no)' >> "$profile/gimprc"
# one window at the page's top left corner, the size of the page, with
# the Layers dock on the right (as gegl-adjustments does it); GIMP
# rewrites the file when it quits
size=${PAS_VIEW:-1400,900}
cat > "$profile/sessionrc" <<SESSION
(session-info "toplevel"
    (factory-entry "gimp-single-image-window")
    (position 0 0)
    (size ${size%,*} ${size#*,})
    (open-on-exit)
    (aux-info
        (left-docks-width "0")
        (right-docks-width "250")
        (maximized "yes"))
    (gimp-dock
        (side right)
        (book
            (current-page 0)
            (dockable "gimp-layer-list"
                (tab-style icon)
                (preview-size 32)
                (aux-info
                    (show-button-bar "true"))))))
(hide-docks no)
(single-window-mode yes)
(show-tabs yes)
SESSION
gimp_run --env=GDK_BACKEND=broadway --env=BROADWAY_DISPLAY=:$display \
  --env=GIMP3_DIRECTORY="$profile" --env=PAS_OUT="$out" \
  --env=PAS_IMAGES="$tests/output/images" \
  --env=PAS_GUI_MODE="${PAS_GUI_MODE:-plugin}" \
  --env=GEGL_PATH="$tests/output/gimp-modules:$(pkg-config --variable=pluginsdir gegl-0.4)" -- sh -c \
  "broadwayd --port $port :$display & bw=\$!; trap 'kill \$bw' EXIT; \
   i=0; while [ ! -f '$out/page-open' ] && [ \$i -lt 300 ]; do sleep 0.2; i=\$((i+1)); done; \
   gimp-3.2 --new-instance --no-splash --no-fonts \
   --batch-interpreter python-fu-eval -b \"exec(open('$here/gui-script.py').read())\""
