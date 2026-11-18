#!/bin/sh
# Times the operation on a 1024 x 1024 sprite sheet (tests/bench.c), for
# each algorithm and factor, built like tests/check.sh against the native
# GEGL, isolated from your folders.
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

gimp_build=${GIMP_BUILD:-$top/../gimp-devtools/gimp-build.sh}
[ -x "$gimp_build" ] || { echo "no gimp-build.sh at $gimp_build (set GIMP_BUILD)"; exit 1; }
in_sdk () { "$gimp_build" "$top" "$*"; }

if [ ! -f "$out/build-check/build.ninja" ]; then
  in_sdk meson setup tests/output/build-check -Dwarning_level=2 >/dev/null
fi
in_sdk ninja -C tests/output/build-check >/dev/null
in_sdk tests/output/build-check/bench tests/output/build-check/pixel-art-scale.so
