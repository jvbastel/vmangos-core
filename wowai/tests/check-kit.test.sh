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

# --- Part 2: two realms ---

kit_out() { "$kit" "$@" 2>&1 | tr -d '\r'; }
project_of() { echo "wowai-t$1-realm"; }
containers_of() { docker ps --all --quiet --filter "label=com.docker.compose.project=$(project_of "$1")"; }
soap_port_of() { echo $((22000 + $1)); }

# A dev install for each realm: a copy of WOWAI_TEST_INSTALL, or one build.
install_for() {
  local ticket="$1" from="$2"
  mkdir -p "$WOWAI_KIT_DIR"
  [ -n "$from" ] && cp -a "$from" "$WOWAI_KIT_DIR/install-t$ticket"
}

started=(998 999)
install_for 998 "${WOWAI_TEST_INSTALL:-}"
echo "Bringing up the realm of the ticket 998..."
"$kit" --ticket 998 up
install_for 999 "$WOWAI_KIT_DIR/install-t998"
echo "Bringing up the realm of the ticket 999..."
"$kit" --ticket 999 up

for ticket in 998 999; do
  check "the realm $ticket is its own Compose project" \
    bash -c "docker compose ls --all --format json | grep -q '\"$(project_of "$ticket")\"'"
  check "the realm $ticket answers SOAP on its own port" \
    bash -c "curl --silent --max-time 5 http://127.0.0.1:$(soap_port_of "$ticket")/ >/dev/null; [ \$? -ne 7 ]"
  check "wait returns at once on the running realm $ticket" "$kit" --ticket "$ticket" wait 30
done
check "the two realms have two database volumes" \
  test "$(docker volume ls --quiet --filter "label=com.docker.compose.project=$(project_of 998)") $(docker volume ls --quiet --filter "label=com.docker.compose.project=$(project_of 999)")" \
  = "$(project_of 998)_database $(project_of 999)_database"

# soap and sql. The bot commands need the GM level of the GM account.
out="$(kit_out --ticket 998 soap 'server info')"
check "soap runs a GM command" contains "$out" "Players online"
# `up` logs the bots in.
online() { kit_out --ticket "$1" sql characters "SELECT online FROM characters WHERE name = '$2'"; }
wait_online() {
  local want="$3"
  for _ in $(seq 1 20); do [ "$(online "$1" "$2")" = "$want" ] && return 0; sleep 1; done
  return 1
}
check "sql reads the bots that up logged in" wait_online 998 Grukk 1
out="$(kit_out --ticket 998 soap '.wowai bot logout Hela')"
check "the GM account may run the bot commands" contains "$out" "logs out"
check "the bot that logged out is offline" wait_online 998 Hela 0
check "the bot of the same name in the other realm is still online" test "$(online 999 Hela)" = 1

# mark and log.
"$kit" --ticket 998 soap '.wowai bot logout Shanka' >/dev/null
wait_online 998 Shanka 0 || true
"$kit" --ticket 998 mark first >/dev/null
"$kit" --ticket 998 soap '.wowai bot login Shanka' >/dev/null
wait_online 998 Shanka 1 || true
out="$(kit_out --ticket 998 log)"
check "log prints the lines since the mark" contains "$out" "[Bots] Shanka logs in"
check "log leaves out the lines before the mark" bash -c "! grep -q 'Grukk logs in\|World initialized' <<<'$out'"
check "log leaves out the known noise" bash -c "! grep -q 'MaNGOSsoap: Accepted connection' <<<'$out'"
check "log --all keeps the noise" contains "$(kit_out --ticket 998 log --all)" "MaNGOSsoap: Accepted connection"
"$kit" --ticket 998 mark second >/dev/null
check "a new mark starts the log again" bash -c "! grep -q 'Shanka logs in' <<<'$(kit_out --ticket 998 log)'"

# restart keeps Server.log and logs the online bots back in.
"$kit" --ticket 998 restart
check "after restart an online bot is online again" wait_online 998 Grukk 1
check "after restart an offline bot stays offline" test "$(online 998 Hela)" = 0
out="$(kit_out --ticket 998 soap 'server info')"
check "after restart mangosd runs GM commands" contains "$out" "Players online"
check "restart keeps the Server.log of before" \
  bash -c "grep -q 'Shanka logs in' '$WOWAI_KIT_DIR'/t998-logs/*-restart/Server.log"

# check runs a script with the kit's client and keeps a pid file.
script="$tmp/kit-check.py"
cat > "$script" <<'PY'
import sys
from kit import Realm
realm = Realm()
print("PORT", realm.world_port)
print("ANSWER", realm.soap("server info"))
print("GUID", realm.guid_of("Grukk"))
client = realm.login("KITTEST", "Kitplayer")
print("LOGGED IN", client.guid, client.race)
if len(sys.argv) > 1:
    import time
    time.sleep(600)
PY
out="$(kit_out --ticket 998 check "$script")"
check "check runs the script on the realm of the ticket" contains "$out" "PORT 21998"
check "the script runs GM commands over the kit" contains "$out" "Players online"
check "the script logs a player in with the client" grep -q "^LOGGED IN [1-9]" <<<"$out"
check "check removes its pid file at the end" \
  bash -c "[ -z \"\$(ls '$WOWAI_KIT_DIR/t998-realm/pids' 2>/dev/null)\" ]"
"$kit" --ticket 998 check "$script" sleep >/dev/null 2>&1 &
runner=$!
for _ in $(seq 1 30); do
  pidfile="$(find "$WOWAI_KIT_DIR/t998-realm/pids" -name '*.pid' 2>/dev/null | head -n 1 || true)"
  [ -n "$pidfile" ] && break
  sleep 1
done
sleeper="$(cut -d' ' -f1 "$pidfile" 2>/dev/null || true)"
check "a running check has a pid file" test -n "$sleeper"

# Before down: no file of another user in the realm folder.
check "the realm folder has only files of the user" \
  test -z "$(find "$WOWAI_KIT_DIR/t998-realm" ! -user "$(id -u)" | head -n 1)"

"$kit" --ticket 998 down
wait "$runner" 2>/dev/null || true
check "down stops the check of the pid file" bash -c "! kill -0 '$sleeper' 2>/dev/null"
check "down leaves the shell of the test running" kill -0 $$
check "down removes the containers" test -z "$(containers_of 998)"
check "down removes the database volume" \
  test -z "$(docker volume ls --quiet --filter "label=com.docker.compose.project=$(project_of 998)")"
check "down removes the realm folder" test ! -e "$WOWAI_KIT_DIR/t998-realm"
kept="$(find "$WOWAI_KIT_DIR/t998-logs" -mindepth 1 -maxdepth 1 -type d ! -name "*-restart" 2>/dev/null | sort | tail -n 1)"
check "down keeps Server.log" bash -c "[ -n '$kept' ] && grep -q 'Grukk logs in' '$kept/mangosd/Server.log'"
check "the kept logs belong to the user" test -z "$(find "$WOWAI_KIT_DIR/t998-logs" ! -user "$(id -u)" | head -n 1)"
out="$(kit_out --ticket 999 soap 'server info')"
check "the other realm still runs" contains "$out" "Players online"

"$kit" --ticket 999 down
check "down of the second realm removes it" test -z "$(containers_of 999)"
started=()
check "nothing in the kit folder belongs to another user" \
  test -z "$(find "$WOWAI_KIT_DIR" ! -user "$(id -u)" | head -n 1)"

if [ "$failures" -gt 0 ]; then
  echo "$failures check(s) failed."
  exit 1
fi
echo "All checks passed."
