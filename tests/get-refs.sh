#!/bin/sh
# Downloads the reference implementations that tests/refcheck.sh builds
# and compares the operation with, into tests/output/refs, and checks each
# file against the SHA-256 sums below. Needs the network (once) and curl.
#
#   - hqx: Cameron Zemek's hqx library (github.com/grom358/hqx, the
#     widely used C version of Maxim Stepin's hq2x, hq3x and hq4x),
#     LGPL-2.1-or-later, at commit a1c7d41 (2023-06-03, the latest)
#   - Scale2x, Scale3x, Scale4x: Andrea Mazzoleni's scale2x 4.0, the
#     release linked from https://www.scale2x.it/download, GPL-2.0-or-later
#   - FFmpeg 8.0.1's vf_hqx.c (ISC), vf_xbr.c (LGPL-2.1-or-later) and
#     vf_epx.c (LGPL-2.1-or-later), from FFmpeg's GitHub mirror at the tag
#     n8.0.1; the sums are those of the same files in the release tarball
#     https://ffmpeg.org/releases/ffmpeg-8.0.1.tar.xz (checked 2026-09-27)
#
# Nothing is installed; the files stay in tests/output/refs.
#
# Copyright 2026 David
# SPDX-License-Identifier: GPL-3.0-or-later
set -e

here=$(cd "$(dirname "$0")" && pwd)
refs=$here/output/refs
mkdir -p "$refs/hqx" "$refs/ffmpeg"

hqx_commit=a1c7d415f4bb7b947c4c2e47faf220b0148f4bac
hqx_url=https://raw.githubusercontent.com/grom358/hqx/$hqx_commit/src
scale2x_url=https://github.com/amadvance/scale2x/releases/download/v4.0/scale2x-4.0.tar.gz
ffmpeg_url=https://raw.githubusercontent.com/FFmpeg/FFmpeg/n8.0.1/libavfilter

sums="982ab5df2b878ea69c76fc33ebe7dc8bcd03985b944b0bc3d9bd5e4c930d1f8d  hqx/common.h
88e57f90efcd7136b42d807568925d0898ce9d579cfe5a9a3e97c24309e92d4e  hqx/hqx.h
98c59c1b868069313465a600ab53a3445974ecf04168db9fceca2a2ce10e3c19  hqx/init.c
1a1bd74d9b72a0ee24774f981c94a270287702964b68d32f2ab59425256c4ade  hqx/hq2x.c
82ed67ccc4f9c98092d39ec9cc84fb5b01a646278d30a396670dc55a147cb39c  hqx/hq3x.c
8be03d6f0b910eabd95b420c3cae14b4c2b471a75c6ffdfb54a77d110446b0ef  hqx/hq4x.c
996f2673206c73fb57f0f5d0e094d3774f595f7e7e80fcca8cc045e8b4ba6d32  scale2x-4.0.tar.gz
09297332f2a66d58460184d44b676280f21a0c7437aec5aba5e2adff37b2c908  ffmpeg/vf_hqx.c
36698f54fc10a136732ee2508d09dcb97b04baf8628a12deffc1bc8ab18dc381  ffmpeg/vf_xbr.c
afdfaa759eb0cd5d8790bea39d7c17be36e7049114bf594268cf14170ab961cc  ffmpeg/vf_epx.c"

get () {
    [ -f "$refs/$2" ] && return
    echo "downloading $1"
    curl -sfL -o "$refs/$2.part" "$1"
    mv "$refs/$2.part" "$refs/$2"
}

for f in common.h hqx.h init.c hq2x.c hq3x.c hq4x.c; do
    get "$hqx_url/$f" "hqx/$f"
done
get "$scale2x_url" scale2x-4.0.tar.gz
for f in vf_hqx.c vf_xbr.c vf_epx.c; do
    get "$ffmpeg_url/$f" "ffmpeg/$f"
done

if ! (cd "$refs" && echo "$sums" | sha256sum -c --quiet -); then
    echo "FAIL  a reference file does not have its SHA-256 sum; remove $refs and try again"
    exit 1
fi
rm -rf "$refs/scale2x-4.0"
tar xzf "$refs/scale2x-4.0.tar.gz" -C "$refs"
echo "PASS  reference sources in $refs, SHA-256 sums checked"
