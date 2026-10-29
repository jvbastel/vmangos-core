#!/usr/bin/env bash
# Test of the pure math of the Forever Warrior mechanics (spec #204, #214):
# src/game/Forever/ForeverWarriorRules.h. That header includes no game header,
# so the host compiler builds its test alone, with no core and no GoogleTest.
# It needs a C++14 compiler (c++, g++ or clang++). Run it from any folder:
#
#   wowai/tests/forever-warrior-rules.test.sh
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
src="$here/../../src/game/Forever/tests/ForeverWarriorRulesTest.cpp"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

cxx="${CXX:-}"
if [ -z "$cxx" ]; then
  for candidate in c++ g++ clang++; do
    if command -v "$candidate" >/dev/null 2>&1; then cxx="$candidate"; break; fi
  done
fi
[ -n "$cxx" ] || { echo "FAIL: no C++ compiler (set CXX)"; exit 1; }

"$cxx" -std=c++14 -Wall -Wextra -Werror -o "$tmp/rules-test" "$src"
"$tmp/rules-test"
