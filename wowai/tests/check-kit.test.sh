#!/usr/bin/env bash
# Test of the in-game check kit: `wowai/test-realm/realm.sh --ticket N ...`.
#
# The first part runs no container: the ticket guard and the names, folders,
# and ports that the kit takes from the ticket number. The second part brings
# up two realms at the same time, for the fake tickets 998 and 999, in a
# temporary kit folder, and runs up, wait, soap, sql, mark, log, restart, and
# down on them. At the end it removes them. It does not touch the shared test
# realm. The second part takes several minutes (a dev build and two fresh
# databases).
#
#   wowai/tests/check-kit.test.sh           Both parts.
#   wowai/tests/check-kit.test.sh --quick   The first part only.
#
# A dev install to copy, so that the test does not build:
# WOWAI_TEST_INSTALL=<folder>. It must match the tree of this test.
# Keep the realms for a look by hand: WOWAI_TEST_KEEP=1.
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
kit="$here/../test-realm/realm.sh"

tmp="$(mktemp -d)"
export WOWAI_KIT_DIR="$tmp/kit"
# The kit must not read these. A check of the guard below sets them.
unset WOWAI_REALM_DIR WOWAI_REALM_PROJECT WOWAI_REALM_LOGIN_PORT WOWAI_REALM_WORLD_PORT WOWAI_REALM_SOAP_PORT WOWAI_DEV_INSTALL

failures=0
pass() { echo "PASS: $*"; }
fail() { echo "FAIL: $*"; failures=$((failures + 1)); }
check() { local name="$1"; shift; if "$@"; then pass "$name"; else fail "$name"; fi; }

# The output and the exit code of a kit command, without `set -e`.
out=""
code=0
run_kit() { code=0; out="$("$kit" "$@" 2>&1)" || code=$?; }
contains() { [[ "$1" == *"$2"* ]]; }

started=()
cleanup() {
  if [ "${WOWAI_TEST_KEEP:-0}" = "1" ]; then
    echo "Kept the realms of the tickets ${started[*]:-} in $tmp."
    return
  fi
  local ticket
  for ticket in "${started[@]}"; do
    "$kit" --ticket "$ticket" down >/dev/null 2>&1 || true
  done
  rm -rf "$tmp"
}
trap cleanup EXIT

# --- Part 1: no containers ---

run_kit up
check "with no --ticket the kit stops" test "$code" -ne 0
check "the error names --ticket" contains "$out" "--ticket"
run_kit sql realmd 'SELECT 1'
check "with no --ticket, sql stops too" test "$code" -ne 0

for bad in abc 0 1000 -3; do
  run_kit --ticket "$bad" env
  check "the ticket number '$bad' is refused" test "$code" -ne 0
done

# A realm setting in the environment does not move the kit to another realm.
code=0
out="$(WOWAI_REALM_DIR="$HOME/wowai/test-realm" WOWAI_REALM_PROJECT=wowai-test-realm \
  WOWAI_REALM_SOAP_PORT=7879 WOWAI_DEV_INSTALL="$HOME/wowai/dev-install" "$kit" --ticket 998 env 2>&1)" || code=$?
check "the kit ignores the realm settings of the environment" \
  bash -c "[ $code -eq 0 ] && ! grep -q 'wowai-test-realm\$\|/wowai/test-realm\$\|dev-install\|=7879\$' <<<'$out'"

run_kit --ticket 998 env
env998="$out"
run_kit --ticket 999 env
env999="$out"
value() { sed -n "s/^$2=//p" <<<"$1"; }
check "the Compose project comes from the ticket" test "$(value "$env998" WOWAI_REALM_PROJECT)" = wowai-t998-realm
check "the realm folder comes from the ticket" test "$(value "$env998" WOWAI_REALM_DIR)" = "$WOWAI_KIT_DIR/t998-realm"
check "the dev install comes from the ticket" test "$(value "$env998" WOWAI_DEV_INSTALL)" = "$WOWAI_KIT_DIR/install-t998"
for name in WOWAI_REALM_PROJECT WOWAI_REALM_DIR WOWAI_DEV_INSTALL WOWAI_REALM_LOGIN_PORT WOWAI_REALM_WORLD_PORT WOWAI_REALM_SOAP_PORT; do
  check "two tickets get two values of $name" \
    bash -c "[ -n '$(value "$env998" "$name")' ] && [ '$(value "$env998" "$name")' != '$(value "$env999" "$name")' ]"
done
ports() { printf '%s\n' "$(value "$1" WOWAI_REALM_LOGIN_PORT)" "$(value "$1" WOWAI_REALM_WORLD_PORT)" "$(value "$1" WOWAI_REALM_SOAP_PORT)"; }
check "the ports of two tickets do not overlap" \
  test "$( (ports "$env998"; ports "$env999"; printf '%s\n' 3725 8086 7879) | sort -u | wc -l)" = 9

run_kit --shared env
check "--shared names the shared realm" bash -c "[ $code -eq 0 ] && grep -q '^WOWAI_REALM_PROJECT=wowai-test-realm$' <<<'$out'"
run_kit --shared down
check "--shared does not remove the shared realm" bash -c "[ $code -ne 0 ] && grep -q 'shared' <<<'$out'"

if [ "${1:-}" = "--quick" ]; then
  [ "$failures" -eq 0 ] || { echo "$failures check(s) failed."; exit 1; }
  echo "All checks passed."
  exit 0
fi
