# shellcheck shell=bash
# The pass and fail harness of the wowai test scripts. Source it:
#
#   . "$here/lib.sh"                 (a test in wowai/tests)
#   . "$here/../../tests/lib.sh"     (a test in wowai/release/tests)
#
#   check <name> <command...>   Run the command: PASS or FAIL with the name.
#   pass <name> / fail <name>   Count a check that the test decides itself.
#   finish                      Print the result and exit: 1 when a check
#                               failed, else 0.
#
# Set quiet_pass=1 before the checks to print only the failures.
# `failures` and `checked` count the failed and all checks.

failures=0
checked=0
quiet_pass=""

pass() {
  checked=$((checked + 1))
  [ -n "$quiet_pass" ] || echo "PASS: $*"
}

fail() {
  checked=$((checked + 1))
  echo "FAIL: $*"
  failures=$((failures + 1))
}

check() {
  local name="$1"
  shift
  if "$@"; then pass "$name"; else fail "$name"; fi
}

finish() {
  if [ "$failures" -ne 0 ]; then
    echo "$failures check(s) failed."
    exit 1
  fi
  echo "All checks passed."
  exit 0
}
