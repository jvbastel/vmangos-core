#!/usr/bin/env bash
# Test of the fork rules check: `wowai/fork-check.sh`.
#
# It makes a throwaway git repository with an "upstream" commit, puts this
# tree's check script and core allowlist on a fork branch, and runs the check
# against the upstream commit after each change. It needs only git and bash.
# Run it from any folder:
#
#   wowai/tests/fork-check.test.sh
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

# shellcheck source=lib.sh
. "$here/lib.sh"

repo="$tmp/repo"
g() { git -C "$repo" "$@"; }
mkdir -p "$repo"
g init -q -b development
g config user.email test@example.com
g config user.name test
mkdir -p "$repo/src/game/Objects" "$repo/src/game/Movement"
echo "void Player::GiveLevel() {}" > "$repo/src/game/Objects/Player.cpp"
echo "class Grid {};" > "$repo/src/game/Movement/Grid.h"
g add -A && g commit -qm upstream
g checkout -qb fork
mkdir -p "$repo/wowai"
cp "$here/../fork-check.sh" "$here/../core-allowlist.txt" "$repo/wowai/"
mkdir -p "$repo/src/bots" "$repo/src/game/Bots"
echo "int Think() { return 0; }" > "$repo/src/bots/Think.cpp"
echo "void Hook() {}" > "$repo/src/game/Bots/BotHooks.cpp"
g add -A && g commit -qm "fork code"

run() { # run <log>: the check of the fork against the upstream branch
  if (cd "$repo" && wowai/fork-check.sh development) > "$tmp/$1" 2>&1; then echo 0; else echo 1; fi
}

echo "Fork with only its own files..."
check "the check passes" test "$(run own.log)" = 0

echo "Change to a core file outside the allowlist..."
echo "class Grid { int x; };" > "$repo/src/game/Movement/Grid.h"
g commit -qam "edit grid"
check "the check fails" test "$(run grid.log)" = 1
check "the failure names the file" grep -q "src/game/Movement/Grid.h" "$tmp/grid.log"

echo "The same change with the file on the allowlist..."
echo "src/game/Movement/Grid.h  # a test hook" >> "$repo/wowai/core-allowlist.txt"
g commit -qam "allow grid"
check "the check passes" test "$(run allowed.log)" = 0

echo "A normal debug log level in the fork's code..."
echo 'void Log() { Out(LOG_LVL_DEBUG, "[Bots] got packet"); }' > "$repo/src/game/Bots/BotLog.cpp"
g add -A && g commit -qm "debug level log"
check "the check passes" test "$(run level.log)" = 0

for dir in src/bots src/game/Bots; do
  echo "A DEBUG line in $dir..."
  g checkout -qb "debug-${dir//\//-}" fork
  echo 'void Think() { Out("[Bots] DEBUG think"); }' > "$repo/$dir/Debug.cpp"
  g add -A && g commit -qm "debug line"
  check "the check fails" test "$(run debug.log)" = 1
  check "the failure names the file and line" grep -q "$dir/Debug.cpp:1" "$tmp/debug.log"
  g checkout -q fork
done

finish
