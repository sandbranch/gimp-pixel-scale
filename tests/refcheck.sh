#!/bin/sh
# Compares the operation byte for byte with the reference implementations
# (tests/refcheck.c): downloads their sources once (tests/get-refs.sh,
# SHA-256 checked), builds them and tests/refcheck.c in the Flatpak SDK
# (gimp-plugin-devtools/gimp-build.sh; natively with GIMP_FLATPAK=0), with
# the operation from tests/output/build-check (built here if missing), and
# runs it. With GIMP_RUN_HOME (tests/output/gimp-home by default) the
# build and the run are isolated from your folders.
#
# Needs the network only for the first download. Exits with 1 if a
# comparison failed.
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

"$here/get-refs.sh" | grep -v '^downloading' || exit 1

gimp_build=${GIMP_BUILD:-$top/../gimp-plugin-devtools/gimp-build.sh}
if [ "${GIMP_FLATPAK:-1}" = 1 ] && flatpak info org.gimp.GIMP >/dev/null 2>&1; then
  [ -x "$gimp_build" ] || { echo "no gimp-build.sh at $gimp_build (set GIMP_BUILD)"; exit 1; }
  in_sdk () { "$gimp_build" "$top" "$*"; }
else
  in_sdk () { (cd "$top" && sh -c "$*"); }
fi

if [ ! -f "$out/build-check/build.ninja" ]; then
  in_sdk meson setup tests/output/build-check -Dwarning_level=2 >/dev/null
fi
in_sdk ninja -C tests/output/build-check >/dev/null

refs=tests/output/refs
b=tests/output/refcheck
rm -rf "$out/refcheck"
mkdir -p "$out/refcheck"
# the same flags for the operation's scalers as in meson.build
cflags="-O2 -g -ffp-contract=off -Wall -Wno-unused-function"
# (the references as they are: their own style of parentheses)
refflags="-Wno-parentheses"
in_sdk "for f in hqx xbr epx; do \
    cc $cflags $refflags -c -DFF_\$(echo \$f | tr a-z A-Z) -Itests/ffshim -I$refs/ffmpeg \
      tests/ffmpeg-ref.c -o $b/ffmpeg-\$f.o || exit 1; \
  done && \
  cc $cflags -Ioperations -I$refs/hqx -I$refs/scale2x-4.0 \
    -DGEGL_PLUGINSDIR=\\\"\$(pkg-config --variable=pluginsdir gegl-0.4)\\\" \
    \$(pkg-config --cflags gegl-0.4) \
    tests/refcheck.c operations/hqx.c operations/xbr.c operations/scalenx.c \
    $refs/hqx/init.c $refs/hqx/hq2x.c $refs/hqx/hq3x.c $refs/hqx/hq4x.c \
    $refs/scale2x-4.0/scalebit.c $refs/scale2x-4.0/scale2x.c $refs/scale2x-4.0/scale3x.c \
    $b/ffmpeg-hqx.o $b/ffmpeg-xbr.o $b/ffmpeg-epx.o \
    \$(pkg-config --libs gegl-0.4) -lm -o $b/refcheck"

status=0
in_sdk "$b/refcheck tests/output/build-check/pixel-art-scale.so" \
  >"$out/refcheck.log" 2>&1 || status=1
grep -E '^(PASS|FAIL|INFO)|passed,|images$' "$out/refcheck.log" || true
grep -qE '^[0-9]+ passed, 0 failed' "$out/refcheck.log" || status=1
exit $status
