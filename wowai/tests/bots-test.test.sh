#!/usr/bin/env bash
# shellcheck disable=SC2329 # check (lib.sh) runs the functions that the test passes to it.
# End-to-end test of the code tests command: `wowai/dev test`.
#
# It copies only `src/bots/`, `bots/`, `dep/include/`, and `wowai/` of this
# working tree into a temporary tree, so the rest of the core is not there. It
# runs the code tests in their own dev container and volume, and removes them
# again at the end.
# Run it from any folder:
#
#   wowai/tests/bots-test.test.sh
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
dev="$here/../dev"
repo="$(git -C "$here" rev-parse --show-toplevel)"

tmp="$(mktemp -d)"
export WOWAI_SRC="$tmp/src"
export WOWAI_DEV_INSTALL="$tmp/install"

# shellcheck source=lib.sh
. "$here/lib.sh"

cleanup() {
  "$dev" clean >/dev/null 2>&1 || true
  rm -rf "$tmp"
}
trap cleanup EXIT

mkdir -p "$WOWAI_SRC/src"
cp -a "$repo/src/bots" "$WOWAI_SRC/src/bots"
cp -a "$repo/wowai" "$WOWAI_SRC/wowai"
cp -a "$repo/bots" "$WOWAI_SRC/bots"
# The header-only libraries that src/bots uses.
mkdir -p "$WOWAI_SRC/dep"
cp -a "$repo/dep/include" "$WOWAI_SRC/dep/include"

echo "Code tests in a tree with only src/bots..."
if "$dev" test > "$tmp/test.log" 2>&1; then status=0; else status=$?; fi
cat "$tmp/test.log"
check "the test command passes without the rest of the core" test "$status" = 0
check "the test command runs ctest" grep -q "tests passed" "$tmp/test.log"
check "the test command runs the tokenizer tests" grep -q "DataTokenizer\\." "$tmp/test.log"
check "the test command runs the fake adapter tests" grep -q "FakeBot\\." "$tmp/test.log"
check "the test command runs the spell data tests" grep -q "SpellData\\." "$tmp/test.log"
check "the test command builds no server" \
  test ! -e "$WOWAI_DEV_INSTALL/bin/mangosd" -a ! -e "$WOWAI_DEV_INSTALL/bin/realmd"

echo "Code tests after a library file includes a game header..."
mkdir -p "$WOWAI_SRC/src/game/Objects"
echo "class Player {};" > "$WOWAI_SRC/src/game/Objects/Player.h"
cat > "$WOWAI_SRC/src/bots/GameHeaderUse.cpp" <<'CPP'
#include "../game/Objects/Player.h"
CPP
sed -i 's/^\(\s*\)DataTokenizer\.cpp$/&\n\1GameHeaderUse.cpp/' "$WOWAI_SRC/src/bots/CMakeLists.txt"
check "the test file is in the library sources" grep -q "GameHeaderUse.cpp" "$WOWAI_SRC/src/bots/CMakeLists.txt"
if "$dev" test > "$tmp/header.log" 2>&1; then status=0; else status=$?; fi
check "the test command fails when the library includes a game header" test "$status" != 0
check "the failure names the game header rule" grep -q "game header" "$tmp/header.log"

finish
