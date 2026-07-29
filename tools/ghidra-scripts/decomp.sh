#!/usr/bin/env bash
# decomp.sh — clean one-command Ghidra decompile by entry-point address.
#
#   tools/ghidra-scripts/decomp.sh 0x00501950 [0xADDR2 ...]
#
# Runs the headless analyzer read-only against the imported kotor1 project,
# has Decompile.java write pure C to a temp file (no log prefixes / doubling /
# banner), and prints that file. ~30-60s (JVM + 28 MB project load) — that
# startup is inherent to Ghidra headless; the slowness is not a bug.
#
# The decompile is PRESERVED on disk after the run (it is never auto-deleted),
# so a caller that pipes stdout into `tail`/`head`/a closed pager can recover
# the full text. The path is printed to stderr; the newest decompile always
# also lives at "$TMPDIR/decomp.latest.txt". Old run files self-expire after a
# day so the temp dir doesn't accumulate. Redirect to a file you control for a
# durable copy:  decomp.sh 0xADDR > mydump.txt
#
# Paths come from the environment so this works on any machine; the defaults
# are the layout docs/tools.md describes. Nothing here ships game data — the
# Ghidra project and the imported executable are yours to supply locally.
#
#   KDEV_GHIDRA_HEADLESS  analyzeHeadless launcher
#   KDEV_GHIDRA_PROJ_DIR  directory holding the Ghidra project
#   KDEV_GHIDRA_PROJ      project name
#   KDEV_GHIDRA_PROGRAM   imported program name inside the project
set -euo pipefail

GHIDRA="${KDEV_GHIDRA_HEADLESS:-C:/Tools/ghidra_12.0.4_PUBLIC/support/analyzeHeadless.bat}"
PROJ_DIR="${KDEV_GHIDRA_PROJ_DIR:-C:/Tools/ghidra-projects}"
PROJ_NAME="${KDEV_GHIDRA_PROJ:-kotor1}"
PROGRAM="${KDEV_GHIDRA_PROGRAM:-k1_win_gog_swkotor.exe}"
# The .java scripts sit next to this file — derive rather than hardcode.
SCRIPTS="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

if [ ! -f "$GHIDRA" ]; then
  echo "decomp.sh: analyzeHeadless not found at '$GHIDRA'." >&2
  echo "           Set KDEV_GHIDRA_HEADLESS to your Ghidra support/ launcher." >&2
  exit 3
fi

if [ "$#" -eq 0 ]; then
  echo "usage: decomp.sh <addr_hex> [<addr_hex> ...]" >&2
  exit 2
fi

# Reap decompiles older than a day so we never auto-delete the run we just
# produced (the bug that ate a fresh dump when the caller's pipe truncated),
# while still keeping the temp dir from growing without bound.
find "${TMPDIR:-/tmp}" -maxdepth 1 -name 'decomp.*.txt' -mtime +0 -delete 2>/dev/null || true

OUT="$(mktemp -t decomp.XXXXXX.txt)"
# Pass OUT first so Decompile.java treats it as the output file.
"$GHIDRA" "$PROJ_DIR" "$PROJ_NAME" \
  -process "$PROGRAM" \
  -readOnly \
  -scriptPath "$SCRIPTS" \
  -postScript Decompile.java "$OUT" "$@" >/dev/null 2>&1 || true

if [ -s "$OUT" ]; then
  # Stable alias to the most recent decompile, plus the durable per-run path.
  LATEST="${TMPDIR:-/tmp}/decomp.latest.txt"
  cp -f "$OUT" "$LATEST" 2>/dev/null || true
  echo "decomp.sh: full output preserved at $OUT (also $LATEST)" >&2
  cat "$OUT"
else
  echo "DECOMP_ERROR: no output produced (project lock? bad address? check $OUT)" >&2
  exit 1
fi
