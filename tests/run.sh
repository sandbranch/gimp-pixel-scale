#!/bin/sh
# All the tests, one after the other:
#
#   tests/check.sh        numerical checks, as usual and with ASan and UBSan
#   tests/refcheck.sh     byte for byte against the reference implementations
#                         (downloads their sources once, SHA-256 checked)
#   tests/gimp-check.sh   the plug-in and the filter in GIMP, without a window
#   tests/gui/gui-test.sh the dialogs in GIMP on Broadway (skipped without
#                         Chrome and node)
#   tests/bench.sh        the times on a 1024 x 1024 sprite sheet (only with
#                         PAS_BENCH=1)
#
# Everything runs isolated from your folders (see tests/isolate.sh); before
# and after, your folders of GIMP and the other apps are listed, and the
# run fails if anything there changed. Exits non-zero if a test failed.
#
# Copyright 2026 David
# SPDX-License-Identifier: GPL-3.0-or-later
here=$(cd "$(dirname "$0")" && pwd)
# (isolate.sh needs $src)
# shellcheck disable=SC2034
src=$(dirname "$here")
out=$here/output
mkdir -p "$out"
GIMP_RUN_HOME=${GIMP_RUN_HOME:-$out/gimp-home}
export GIMP_RUN_HOME
# shellcheck source=SCRIPTDIR/isolate.sh
. "$here/isolate.sh"
snapshot_take "$out/snapshot-run-before.txt"
status=0

for t in check.sh refcheck.sh gimp-check.sh gui/gui-test.sh; do
    echo "== tests/$t"
    "$here/$t" || status=1
    echo
done
if [ "${PAS_BENCH:-0}" = 1 ]; then
    echo "== tests/bench.sh"
    "$here/bench.sh" || status=1
    echo
fi

snapshot_check "$out/snapshot-run-before.txt" "" || status=1
[ $status = 0 ] && echo "all tests passed" || echo "SOME TESTS FAILED"
exit $status
