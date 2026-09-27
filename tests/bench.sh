#!/bin/sh
# Times the operation on a 1024 x 1024 sprite sheet (tests/bench.c), for
# each algorithm and factor, in the Flatpak SDK like tests/check.sh (or
# natively with GIMP_FLATPAK=0), isolated from your folders.
#
# Copyright 2026 David
# SPDX-License-Identifier: GPL-3.0-or-later
set -e

here=$(cd "$(dirname "$0")" && pwd)
top=$(dirname "$here")
out="$here/output"
mkdir -p "$out"
GIMP_RUN_HOME=${GIMP_RUN_HOME:-$out/gimp-home}
export GIMP_RUN_HOME

gimp_build=${GIMP_BUILD:-$top/../gimp-plugin-devtools/gimp-build.sh}
if [ "${GIMP_FLATPAK:-1}" = 1 ] && flatpak info org.gimp.GIMP >/dev/null 2>&1; then
  in_sdk () { "$gimp_build" "$top" "$*"; }
else
  in_sdk () { (cd "$top" && sh -c "$*"); }
fi

if [ ! -f "$out/build-check/build.ninja" ]; then
  in_sdk meson setup tests/output/build-check -Dwarning_level=2 >/dev/null
fi
in_sdk ninja -C tests/output/build-check >/dev/null
in_sdk tests/output/build-check/bench tests/output/build-check/pixel-art-scale.so
