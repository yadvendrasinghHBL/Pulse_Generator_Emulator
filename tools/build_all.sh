#!/usr/bin/env bash
# Definition-of-done build (AGENTS.md): debug and release from a clean build/ folder,
# failing on any compiler warning in our code (vendor code is built with -w).
# Used by tools/build_and_flash.sh, the GitHub Actions CI and the Zed "build: all" task.
#
#   tools/build_all.sh            # clean build of both
#   tools/build_all.sh --keep     # incremental (keep build/), faster while editing
set -euo pipefail
cd "$(dirname "$0")/.."

[ "${1:-}" = "--keep" ] || rm -rf build
LOG=$(mktemp)
trap 'rm -f "$LOG"' EXIT
{
  cmake --preset debug && cmake --build --preset debug &&
  cmake --preset release && cmake --build --preset release
} 2>&1 | tee "$LOG"
WARN=$(grep -c 'warning:' "$LOG" || true)
if [ "$WARN" != 0 ]; then
  echo "ERR $WARN compiler warning(s):" >&2
  grep 'warning:' "$LOG" >&2
  exit 1
fi
echo "OK 2 builds (debug, release), 0 warnings"
