#!/usr/bin/env bash
# Checks two rules of the WOWAI fork. Run it from the root of the fork:
#
#   wowai/fork-check.sh [<upstream-ref>]
#
# 1. Core files: each file that the fork changed since it forked from
#    <upstream-ref> (default `upstream/development`) must match a pattern in
#    `wowai/core-allowlist.txt`. The fork's own folders are there, and the
#    core files that the bots hook into. A new hook is one new line there.
# 2. Debug lines: the fork's own code has no `DEBUG` marker, such as the
#    `[Bots] DEBUG` log lines of a debug session. `LOG_LVL_DEBUG` is fine.
#
# The check needs the history down to the fork point (CI: fetch-depth 0).
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
upstream="${1:-upstream/development}"
allowlist="$here/core-allowlist.txt"
failures=0

base="$(git merge-base HEAD "$upstream")" || {
  echo "fork-check: no common commit of HEAD and $upstream. Fetch the full history." >&2
  exit 2
}

patterns=()
while IFS= read -r line || [ -n "$line" ]; do
  line="${line%%#*}"
  line="${line//[[:space:]]/}"
  [ -n "$line" ] && patterns+=("$line")
done < "$allowlist"

allowed() {
  local pattern
  for pattern in "${patterns[@]}"; do
    # In [[ == ]], `*` also matches `/`, so `dir/**` covers the whole folder.
    # shellcheck disable=SC2053
    [[ "$1" == $pattern ]] && return 0
  done
  return 1
}

while IFS= read -r file; do
  if ! allowed "$file"; then
    echo "fork-check: $file is a core file outside wowai/core-allowlist.txt."
    failures=$((failures + 1))
  fi
done < <(git diff --name-only --no-renames "$base" HEAD)

# Upstream has no src/bots or src/game/Bots, so each marker there is new.
while IFS= read -r hit; do
  echo "fork-check: DEBUG marker at $hit"
  failures=$((failures + 1))
done < <(git grep -n -w DEBUG -- src/bots src/game/Bots || true)

if [ "$failures" -ne 0 ]; then
  echo "fork-check: $failures problem(s). Remove DEBUG lines. Move a core change into the fork's own code, or add the hook file to wowai/core-allowlist.txt."
  exit 1
fi
echo "fork-check: all files are on the allowlist, and no DEBUG lines."
