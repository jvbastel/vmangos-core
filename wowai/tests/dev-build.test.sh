#!/usr/bin/env bash
# End-to-end test of the dev build: `wowai/dev build`.
#
# It clones the fork into a temporary tree, builds it in its own dev
# container and volume, and removes them again at the end. A first build
# takes a few minutes. Run it from any folder:
#
#   wowai/tests/dev-build.test.sh
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
dev="$here/../dev"
repo="$(git -C "$here" rev-parse --show-toplevel)"

tmp="$(mktemp -d)"
export WOWAI_SRC="$tmp/src"
export WOWAI_DEV_INSTALL="$tmp/install"

failures=0
pass() { echo "PASS: $*"; }
fail() { echo "FAIL: $*"; failures=$((failures + 1)); }
check() { local name="$1"; shift; if "$@"; then pass "$name"; else fail "$name"; fi; }

cleanup() {
  "$dev" clean >/dev/null 2>&1 || true
  rm -rf "$tmp"
}
trap cleanup EXIT

git clone --quiet --shared "$repo" "$WOWAI_SRC"
git -C "$WOWAI_SRC" config core.autocrlf false

echo "First dev build..."
"$dev" build

check "the install folder has mangosd and realmd" \
  test -x "$WOWAI_DEV_INSTALL/bin/mangosd" -a -x "$WOWAI_DEV_INSTALL/bin/realmd"
check "the source tree stays clean (the patches are not in it)" \
  test -z "$(git -C "$WOWAI_SRC" status --porcelain)"

if [ "$failures" -ne 0 ]; then
  echo "$failures check(s) failed."
  exit 1
fi
echo "All checks passed."
