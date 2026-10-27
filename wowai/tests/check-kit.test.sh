#!/usr/bin/env bash
# shellcheck disable=SC2329 # check (lib.sh) runs the functions that the test passes to it.
# Test of the in-game check kit: `wowai/test-realm/realm.sh --ticket N ...`.
#
# The first part runs no container: the ticket guard. (realm-pool.test.sh
# tests the leases with no container.) The second part leases two slots of
# the real pool at the same time, for the fake tickets 998 and 999, and runs
# acquire, wait, soap, sql, mark, log, restart, check, and release on them.
# Then the fake ticket 997 leases the slot of 998 again, and gets the
# baseline database. At the end it releases the slots. It does not touch the
# shared test realm. The second part takes several minutes (a dev build, and
# a baseline if the pool has none), and needs two free slots.
#
#   wowai/tests/check-kit.test.sh           Both parts.
#   wowai/tests/check-kit.test.sh --quick   The first part only.
#
# Keep the slots leased for a look by hand: WOWAI_TEST_KEEP=1.
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
kit="$here/../test-realm/realm.sh"

tmp="$(mktemp -d)"
pool="$(realpath -m "${WOWAI_POOL_DIR:-$HOME/wowai/slots}")"
# The defaults of the shared test realm.
# shellcheck source=../test-realm/defaults.sh
. "$here/../test-realm/defaults.sh"
shared_project="${realm_defaults[WOWAI_REALM_PROJECT]}"
# The kit must not read these. A check of the guard below sets them.
unset "${!realm_defaults[@]}"

# shellcheck source=lib.sh
. "$here/lib.sh"

# The output and the exit code of a kit command, without `set -e`.
out=""
code=0
run_kit() { code=0; out="$("$kit" "$@" 2>&1)" || code=$?; }
contains() { [[ "$1" == *"$2"* ]]; }

started=()
cleanup() {
  if [ "${WOWAI_TEST_KEEP:-0}" = "1" ]; then
    echo "The tickets ${started[*]:-} keep their slots. Release them with: realm.sh --ticket <N> release"
    return
  fi
  local ticket
  for ticket in "${started[@]}"; do
    "$kit" --ticket "$ticket" release >/dev/null 2>&1 || true
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

run_kit --shared env
check "--shared names the shared realm" bash -c "[ $code -eq 0 ] && grep -qxF 'WOWAI_REALM_PROJECT=$shared_project' <<<'$out'"
run_kit --ticket 998 env
check "a ticket with no slot gets an error" test "$code" -ne 0
run_kit --shared down
check "--shared does not remove the shared realm" bash -c "[ $code -ne 0 ] && grep -q 'shared' <<<'$out'"

if [ "${1:-}" = "--quick" ]; then
  finish
fi

# --- Part 2: two slots ---

kit_out() { "$kit" "$@" 2>&1 | tr -d '\r'; }
slot_of() { kit_out --ticket "$1" env | sed -n 's/^WOWAI_REALM_PROJECT=wowai-test-//p'; }
project_of() { echo "wowai-test-$(slot_of "$1")"; }
containers_of() { docker ps --all --quiet --filter "label=com.docker.compose.project=$1"; }
running_of() { docker ps --quiet --filter "label=com.docker.compose.project=$1"; }
soap_port_of() { echo $((7880 + $(slot_of "$1"))); }

started=(998 999)
echo "Acquiring a slot for the ticket 998..."
"$kit" --ticket 998 acquire
echo "Acquiring a slot for the ticket 999..."
"$kit" --ticket 999 acquire
slot998="$(slot_of 998)"
project998="$(project_of 998)"
realm998="$pool/$slot998"
logs998="$pool/logs/t998"

for ticket in 998 999; do
  check "the realm $ticket is its own Compose project" \
    bash -c "docker compose ls --all --format json | grep -q '\"$(project_of "$ticket")\"'"
  check "the realm $ticket answers SOAP on its own port" \
    bash -c "curl --silent --max-time 5 http://127.0.0.1:$(soap_port_of "$ticket")/ >/dev/null; [ \$? -ne 7 ]"
  check "wait returns at once on the running realm $ticket" "$kit" --ticket "$ticket" wait 30
done
check "the two slots have two database volumes" \
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
  bash -c "grep -q 'Shanka logs in' '$logs998'/*-restart/Server.log"

# check runs a script with the kit's client and keeps a pid file.
script="$tmp/kit-check.py"
cat > "$script" <<'PY'
import sys
from kit import Realm
realm = Realm()
print("PORT", realm.world_port)
print("ANSWER", realm.soap("server info"))
print("GUID", realm.guid_of("Grukk"))
client = realm.login("KITTEST", "Thrallson")
print("LOGGED IN", client.guid, client.race)
if len(sys.argv) > 1:
    import time
    time.sleep(600)
PY
out="$(kit_out --ticket 998 check "$script")" || true
echo "$out"
check "check runs the script on the slot of the ticket" contains "$out" "PORT $((8090 + slot998))"
check "the script runs GM commands over the kit" contains "$out" "Players online"
check "the script logs a player in with the client" grep -q "^LOGGED IN [1-9]" <<<"$out"
check "check removes its pid file at the end" \
  bash -c "[ -z \"\$(ls '$realm998/pids' 2>/dev/null)\" ]"
"$kit" --ticket 998 check "$script" sleep >/dev/null 2>&1 &
runner=$!
for _ in $(seq 1 30); do
  pidfile="$(find "$realm998/pids" -name '*.pid' 2>/dev/null | head -n 1 || true)"
  [ -n "$pidfile" ] && break
  sleep 1
done
sleeper="$(cut -d' ' -f1 "$pidfile" 2>/dev/null || true)"
check "a running check has a pid file" test -n "$sleeper"

# Before release: no file of another user in the realm folder.
check "the realm folder has only files of the user" \
  test -z "$(find "$realm998" ! -user "$(id -u)" | head -n 1)"

# A migration of the ticket, to see that the next lease does not get it.
"$kit" --ticket 998 sql characters "INSERT INTO migrations (id) VALUES ('29991231000000')" >/dev/null

"$kit" --ticket 998 release
wait "$runner" 2>/dev/null || true
check "release stops the check of the pid file" bash -c "! kill -0 '$sleeper' 2>/dev/null"
check "release leaves the shell of the test running" kill -0 $$
check "release keeps the containers of the slot" test -n "$(containers_of "$project998")"
check "release stops the slot" test -z "$(running_of "$project998")"
check "release keeps the database volume" \
  test -n "$(docker volume ls --quiet --filter "label=com.docker.compose.project=$project998")"
check "release removes the lease" test ! -e "$pool/$slot998.lease"
kept="$(find "$logs998" -mindepth 1 -maxdepth 1 -type d ! -name "*-restart" 2>/dev/null | sort | tail -n 1)"
check "release keeps Server.log" bash -c "[ -n '$kept' ] && grep -q 'Grukk logs in' '$kept/mangosd/Server.log'"
check "the kept logs belong to the user" test -z "$(find "$logs998" ! -user "$(id -u)" | head -n 1)"
out="$(kit_out --ticket 999 soap 'server info')"
check "the other slot still runs" contains "$out" "Players online"

# The next ticket on the slot starts from the baseline.
started=(999 997)
echo "Acquiring a slot for the ticket 997..."
"$kit" --ticket 997 acquire
check "the next ticket gets the free slot" test "$(slot_of 997)" = "$slot998"
check "the slot has no character of the last ticket" \
  test -z "$(kit_out --ticket 997 sql characters "SELECT name FROM characters WHERE name = 'Thrallson'")"
check "the slot has no migration of the last ticket" \
  test -z "$(kit_out --ticket 997 sql characters "SELECT id FROM migrations WHERE id = '29991231000000'")"
check "the slot has the bots again" test "$(kit_out --ticket 997 sql characters "SELECT name FROM characters WHERE name = 'Grukk'")" = Grukk
check "the slot runs GM commands" contains "$(kit_out --ticket 997 soap 'server info')" "Players online"

for ticket in 997 999; do
  "$kit" --ticket "$ticket" release
done
started=()
check "the logs of the pool belong to the user" \
  test -z "$(find "$pool/logs" ! -user "$(id -u)" | head -n 1)"

finish
