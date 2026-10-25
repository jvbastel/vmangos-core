#!/usr/bin/env bash
# End-to-end test of the dev container lock and the shared install guard of
# `wowai/dev`.
#
# It uses an empty source tree in its own dev container, so that it builds no
# core, and removes the container again at the end. Run it from any folder:
#
#   wowai/tests/dev-lock.test.sh
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
dev="$here/../dev"

tmp="$(mktemp -d)"
export WOWAI_SRC="$tmp/src"
export WOWAI_DEV_INSTALL="$tmp/install-a"
mkdir -p "$WOWAI_SRC"

failures=0
pass() { echo "PASS: $*"; }
fail() { echo "FAIL: $*"; failures=$((failures + 1)); }
check() { local name="$1"; shift; if "$@"; then pass "$name"; else fail "$name"; fi; }

cleanup() {
  "$dev" clean >/dev/null 2>&1 || true
  rm -rf "$tmp"
}
trap cleanup EXIT

dev_container() { docker ps --all --quiet --filter "label=wowai.dev.src=$WOWAI_SRC"; }

# Wait until a process with this command line runs in the dev container.
wait_for_process() {
  local pattern="$1" i
  for i in $(seq 1 120); do
    if [ -n "$(dev_container)" ] && docker top "$(dev_container)" 2>/dev/null | grep -q -- "$pattern"; then
      return 0
    fi
    sleep 1
  done
  return 1
}

echo "Two calls at the same time with other install folders..."
"$dev" shell bash -c 'sleep 15; echo first-call-done' > "$tmp/first.log" 2>&1 &
first=$!
wait_for_process "sleep 15" || { cat "$tmp/first.log"; echo "The first call did not start."; exit 1; }
if WOWAI_DEV_INSTALL="$tmp/install-b" "$dev" shell true > "$tmp/second.log" 2>&1; then second_status=0; else second_status=$?; fi
if wait "$first"; then first_status=0; else first_status=$?; fi
cat "$tmp/first.log" "$tmp/second.log"
check "the first call runs to the end (exit $first_status)" \
  bash -c "[ '$first_status' = 0 ] && grep -q first-call-done '$tmp/first.log'"
check "the second call waits for the first and passes (exit $second_status)" \
  bash -c "[ '$second_status' = 0 ] && grep -q 'Waiting' '$tmp/second.log'"
touch "$tmp/install-b/second-install"
check "the container now mounts the second install folder" \
  env WOWAI_DEV_INSTALL="$tmp/install-b" "$dev" shell test -e /opt/vmangos/second-install

echo "A call with another install folder while a command without the lock runs..."
docker exec --detach "$(dev_container)" sleep 300
wait_for_process "sleep 300" || fail "the command without the lock did not start"
if WOWAI_DEV_INSTALL="$tmp/install-c" "$dev" shell true > "$tmp/busy.log" 2>&1; then busy_status=0; else busy_status=$?; fi
cat "$tmp/busy.log"
check "the call stops with an error (exit $busy_status)" test "$busy_status" != 0
check "the error names the busy dev container" grep -q "runs in the dev container" "$tmp/busy.log"
check "the command without the lock still runs" wait_for_process "sleep 300"
"$dev" stop

echo "A build into the shared dev install folder..."
# A home folder of the test, so that the default install folder is in it.
# The Docker settings stay those of the user.
shared_env=(env -u WOWAI_DEV_INSTALL HOME="$tmp/home" DOCKER_CONFIG="${DOCKER_CONFIG:-$HOME/.docker}")
mkdir -p "$tmp/home"
if "${shared_env[@]}" "$dev" build > "$tmp/shared.log" 2>&1; then shared_status=0; else shared_status=$?; fi
cat "$tmp/shared.log"
check "a build with no WOWAI_DEV_INSTALL stops (exit $shared_status)" test "$shared_status" != 0
check "the error names WOWAI_DEV_INSTALL and WOWAI_DEV_SHARED_INSTALL" \
  bash -c "grep -q WOWAI_DEV_INSTALL '$tmp/shared.log' && grep -q WOWAI_DEV_SHARED_INSTALL '$tmp/shared.log'"
check "the build did not start" test ! -e "$tmp/home/wowai/dev-install"
if "${shared_env[@]}" WOWAI_DEV_INSTALL="$tmp/home/wowai/dev-install" "$dev" build > "$tmp/shared-set.log" 2>&1; then shared_status=0; else shared_status=$?; fi
check "a build with WOWAI_DEV_INSTALL set to the shared folder stops too (exit $shared_status)" \
  bash -c "[ '$shared_status' != 0 ] && grep -q WOWAI_DEV_SHARED_INSTALL '$tmp/shared-set.log'"
# The empty source tree fails later, at the patches. The guard lets it pass.
"${shared_env[@]}" WOWAI_DEV_SHARED_INSTALL=1 "$dev" build > "$tmp/shared-on.log" 2>&1 || true
check "WOWAI_DEV_SHARED_INSTALL=1 lets the build use the shared folder" \
  bash -c "! grep -q WOWAI_DEV_SHARED_INSTALL '$tmp/shared-on.log' && [ -d '$tmp/home/wowai/dev-install' ]"
"${shared_env[@]}" "$dev" clean >/dev/null 2>&1 || true

if [ "$failures" -ne 0 ]; then
  echo "$failures check(s) failed."
  exit 1
fi
echo "All checks passed."
