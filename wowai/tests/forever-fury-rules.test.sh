#!/usr/bin/env bash
# Test of the pure math of the Forever Fury talents (spec #204, #218):
# src/game/Forever/ForeverFuryRules.h. That header includes no game header,
# so the host compiler builds its test alone, with no core and no GoogleTest.
# It needs a C++14 compiler (c++, g++ or clang++). Run it from any folder:
#
#   wowai/tests/forever-fury-rules.test.sh
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
src="$here/../../src/game/Forever/tests/ForeverFuryRulesTest.cpp"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

cxx="${CXX:-}"
if [ -z "$cxx" ]; then
  for candidate in c++ g++ clang++; do
    if command -v "$candidate" >/dev/null 2>&1; then cxx="$candidate"; break; fi
  done
fi
[ -n "$cxx" ] || { echo "FAIL: no C++ compiler (set CXX)"; exit 1; }

"$cxx" -std=c++14 -Wall -Wextra -Werror -o "$tmp/fury-rules-test" "$src"
"$tmp/fury-rules-test"
