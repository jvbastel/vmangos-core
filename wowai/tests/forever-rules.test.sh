#!/usr/bin/env bash
# Test of the pure Forever rules (spec jvbastel/WOWAI#204): each
# src/game/Forever/tests/*Test.cpp tests a header of src/game/Forever that
# includes no game header (the *Rules.h files, ForeverSpellIds.h), so the host
# compiler builds each test alone, with no core and no GoogleTest. The tests
# share the harness tests/TestCheck.h. It needs a C++14 compiler (c++, g++ or
# clang++; or set CXX). Run it from any folder:
#
#   wowai/tests/forever-rules.test.sh            # every test
#   wowai/tests/forever-rules.test.sh Fury       # the tests whose name has "Fury"
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
tests="$here/../../src/game/Forever/tests"
filter="${1:-}"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

cxx="${CXX:-}"
if [ -z "$cxx" ]; then
  for candidate in c++ g++ clang++; do
    if command -v "$candidate" >/dev/null 2>&1; then cxx="$candidate"; break; fi
  done
fi
[ -n "$cxx" ] || { echo "FAIL: no C++ compiler (set CXX)"; exit 1; }

ran=0
failed=()
for src in "$tests"/*Test.cpp; do
  name="$(basename "$src" .cpp)"
  [[ -z "$filter" || "$name" == *"$filter"* ]] || continue
  echo "== $name"
  ran=$((ran + 1))
  if ! "$cxx" -std=c++14 -Wall -Wextra -Werror -o "$tmp/$name" "$src"; then
    failed+=("$name (build)")
    continue
  fi
  "$tmp/$name" || failed+=("$name")
done

[ "$ran" -gt 0 ] || { echo "FAIL: no test matches '$filter'"; exit 1; }
if [ "${#failed[@]}" -gt 0 ]; then
  echo "FAIL: ${failed[*]}"
  exit 1
fi
echo "PASS: $ran Forever rules tests"
