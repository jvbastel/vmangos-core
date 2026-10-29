#!/usr/bin/env bash
# shellcheck disable=SC2329 # check (lib.sh) runs the functions that the test passes to it.
# Test of the test realm pool of the check kit: the leases of `realm.sh
# --ticket N acquire | release` and `realm.sh gc`. It runs no container.
#
# The lease part runs realm.sh with a fake dev script (WOWAI_KIT_DEV), which
# prints the command and the settings that the kit gives it. The release and
# gc part runs the real wowai/dev with the fake-docker helper of the release
# tests, which logs each docker call.
#
#   wowai/tests/realm-pool.test.sh
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
kit="$here/../test-realm/realm.sh"
repo="$(git -C "$here" rev-parse --show-toplevel)"
branch="$(git -C "$here" rev-parse --abbrev-ref HEAD)"

# shellcheck source=lib.sh
. "$here/lib.sh"

tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

export WOWAI_POOL_DIR="$tmp/slots"
export WOWAI_DATABASE_IMAGE=wowai/vmangos-database:t1
export WOWAI_BOTCHAT_SRC="$tmp/botchat"
mkdir -p "$tmp/bin" "$WOWAI_POOL_DIR"
ln -s "$here/../release/tests/fake-docker" "$tmp/bin/docker"
export PATH="$tmp/bin:$PATH"
export FAKE_DOCKER_LOG="$tmp/docker.log" FAKE_DOCKER_IMAGES="$tmp/images"
: > "$FAKE_DOCKER_LOG"

# The fake dev script: prints its arguments and the WOWAI_ settings.
cat > "$tmp/fake-dev" <<'SH'
#!/usr/bin/env bash
echo "dev: $*"
env | grep '^WOWAI_' | sort
# A baseline: its passwords file.
if [ "$*" = "realm baseline" ]; then
  sed -e "s/:t1$/:${WOWAI_DATABASE_IMAGE##*:}/" -e 's/basegm/newgm/' "$WOWAI_POOL_DIR/baseline-t1.env" > "$WOWAI_REALM_BASELINE_ENV"
fi
SH
chmod +x "$tmp/fake-dev"
export WOWAI_KIT_DEV="$tmp/fake-dev"

# The baseline of the image tag t1 exists (the fake docker knows each volume).
cat > "$WOWAI_POOL_DIR/baseline-t1.env" <<'EOF'
WOWAI_DATABASE_IMAGE=wowai/vmangos-database:t1
WOWAI_REALM_DB_PASSWORD=basedb
WOWAI_REALM_DB_ROOT_PASSWORD=baseroot
WOWAI_REALM_BOTCHAT_DB_PASSWORD=basechat
WOWAI_REALM_GM_ACCOUNT=GM
WOWAI_REALM_GM_PASSWORD=basegm
EOF

out=""
code=0
run_kit() { code=0; out="$("$kit" "$@" 2>&1)" || code=$?; }
contains() { [[ "$1" == *"$2"* ]]; }
value() { sed -n "s/^$2=//p" <<<"$1" | tail -n 1; }
slot_of() { { grep -lx "ticket=$1" "$WOWAI_POOL_DIR"/*.lease 2>/dev/null || true; } | sed 's|.*/||; s|\.lease$||'; }

# --- Leases ---

run_kit --ticket 11 up
check "a ticket with no lease gets an error" test "$code" -ne 0
check "the error names acquire" contains "$out" "acquire"

# Two tickets acquire at the same time.
"$kit" --ticket 11 acquire > "$tmp/a11" 2>&1 &
first=$!
"$kit" --ticket 12 acquire > "$tmp/a12" 2>&1 &
second=$!
wait "$first" || true; wait "$second" || true
s11="$(slot_of 11)"
s12="$(slot_of 12)"
check "two tickets that acquire at once get two slots" \
  bash -c "[ -n '$s11' ] && [ -n '$s12' ] && [ '$s11' != '$s12' ]"
check "a slot is a number of the pool" bash -c "[[ '$s11 $s12' =~ ^[1-5]\ [1-5]$ ]]"
check "a new lease resets the slot" contains "$(cat "$tmp/a11")" "dev: realm acquire new"
lease="$(cat "$WOWAI_POOL_DIR/$s11.lease")"
check "the lease names the worktree" test "$(value "$lease" worktree)" = "$repo"
check "the lease names the branch" test "$(value "$lease" branch)" = "$branch"
check "the lease has the start time" test -n "$(value "$lease" started)"

run_kit --ticket 11 acquire
check "a ticket that acquires twice gets the same slot" test "$(value "$out" WOWAI_REALM_SLOT)" = "$s11"
check "a second acquire does not reset the slot" contains "$out" "dev: realm acquire held"
check "the ticket still has one lease" test "$(grep -lx "ticket=11" "$WOWAI_POOL_DIR"/*.lease | wc -l)" = 1
check "acquire uses the chat service of the caller" test "$(value "$out" WOWAI_BOTCHAT_SRC)" = "$WOWAI_BOTCHAT_SRC"
check "a new lease uses the passwords of the baseline" test "$(value "$(cat "$tmp/a11")" WOWAI_REALM_GM_PASSWORD)" = basegm
check "a new lease uses the baseline volume of the image tag" \
  test "$(value "$(cat "$tmp/a11")" WOWAI_REALM_BASELINE)" = wowai-test-baseline-t1

# The other commands find the slot from the lease.
run_kit --ticket 11 soap 'server info'
check "a command runs on the slot of the lease" contains "$out" "dev: realm soap server info"
check "the Compose project is the slot's" test "$(value "$out" WOWAI_REALM_PROJECT)" = "wowai-test-$s11"
check "the realm folder is the slot's" test "$(value "$out" WOWAI_REALM_DIR)" = "$WOWAI_POOL_DIR/$s11"
check "the dev install is the slot's" test "$(value "$out" WOWAI_DEV_INSTALL)" = "$WOWAI_POOL_DIR/$s11/install"
check "the login port is 3730 + the slot" test "$(value "$out" WOWAI_REALM_LOGIN_PORT)" = $((3730 + s11))
check "the world port is 8090 + the slot" test "$(value "$out" WOWAI_REALM_WORLD_PORT)" = $((8090 + s11))
check "the SOAP port is 7880 + the slot" test "$(value "$out" WOWAI_REALM_SOAP_PORT)" = $((7880 + s11))
check "the dev container is the slot's" test "$(value "$out" WOWAI_DEV_ID)" = "slot-$s11"

out="$(WOWAI_REALM_DIR="$tmp/shared" WOWAI_REALM_PROJECT=wowai-test-realm WOWAI_REALM_SOAP_PORT=7879 \
  WOWAI_DEV_INSTALL="$tmp/shared-install" "$kit" --ticket 11 env 2>&1)"
check "the realm settings of the environment do not move the kit" \
  bash -c "[ '$(value "$out" WOWAI_REALM_PROJECT)' = 'wowai-test-$s11' ] && [ '$(value "$out" WOWAI_REALM_SOAP_PORT)' = $((7880 + s11)) ] &&
    [ '$(value "$out" WOWAI_DEV_INSTALL)' = '$WOWAI_POOL_DIR/$s11/install' ]"

# A new database image: a new lease gets its baseline, and a held lease keeps
# the passwords of its database volume.
WOWAI_DATABASE_IMAGE=wowai/vmangos-database:t2 run_kit --ticket 13 acquire
check "with no baseline of the image tag, a new lease makes it" contains "$out" "dev: realm baseline"
check "the baseline is its own Compose project" grep -qx "WOWAI_REALM_PROJECT=wowai-test-baseline" <<<"$out"
check "the new lease uses the new baseline" test "$(value "$out" WOWAI_REALM_GM_PASSWORD)" = newgm
WOWAI_DATABASE_IMAGE=wowai/vmangos-database:t2 run_kit --ticket 11 acquire
check "a held lease makes no baseline" bash -c "! grep -q 'dev: realm baseline' <<<'$out'"
check "a held lease keeps the passwords of its volume" test -z "$(value "$out" WOWAI_REALM_GM_PASSWORD)"

# A full pool.
for ticket in 14 15; do
  "$kit" --ticket "$ticket" acquire >/dev/null 2>&1
done
run_kit --ticket 16 acquire
check "a sixth ticket gets an error" test "$code" -ne 0
for ticket in 11 12 13 14 15; do
  check "the full-pool error names the holder $ticket" grep -q "ticket $ticket, worktree $repo, branch $branch" <<<"$out"
done
check "the sixth ticket gets no lease" test -z "$(slot_of 16)"

run_kit --shared acquire
check "--shared does not lease a slot" test "$code" -ne 0

# --- release and gc, with the real dev script and the fake docker ---

unset WOWAI_KIT_DEV

# wowai/dev outside the kit: a tree that leases a slot uses the slot's dev
# container, and another tree its own.
"$repo/wowai/dev" stop
check "a leased tree uses the dev container of its slot" \
  bash -c "tail -n 1 '$FAKE_DOCKER_LOG' | grep -qE ' stop wowai-dev-slot-[1-5]$'"
WOWAI_POOL_DIR="$tmp/no-slots" "$repo/wowai/dev" stop
check "a tree with no lease uses a dev container of its own" \
  bash -c "tail -n 1 '$FAKE_DOCKER_LOG' | grep -qE ' stop wowai-dev-$(basename "$repo")-[0-9a-f]{8}$'"
code=0; WOWAI_DEV_ID='a b' "$repo/wowai/dev" stop 2>/dev/null || code=$?
check "WOWAI_DEV_ID must be a plain name" test "$code" -ne 0

mkdir -p "$WOWAI_POOL_DIR/$s11/storage/mangosd/logs"
echo "the log of ticket 11" > "$WOWAI_POOL_DIR/$s11/storage/mangosd/logs/Server.log"
run_kit --ticket 11 release
check "release succeeds" test "$code" -eq 0
check "release removes the lease" test ! -e "$WOWAI_POOL_DIR/$s11.lease"
check "release keeps Server.log in the ticket's log folder" \
  bash -c "grep -qs 'the log of ticket 11' '$WOWAI_POOL_DIR'/logs/t11/*/mangosd/Server.log"
check "release stops the slot" grep -q "compose --project-name wowai-test-$s11 stop" "$FAKE_DOCKER_LOG"
check "release stops the slot's dev container" grep -q " stop wowai-dev-slot-$s11$" "$FAKE_DOCKER_LOG"
check "release keeps the slot folder" test -d "$WOWAI_POOL_DIR/$s11/storage"
run_kit --ticket 11 soap 'server info'
check "after release the ticket has no slot" test "$code" -ne 0

# A lease of a worktree that no longer exists.
sed -i "s|^worktree=.*|worktree=$tmp/gone|" "$WOWAI_POOL_DIR/$s12.lease"
mkdir -p "$WOWAI_POOL_DIR/$s12/storage/mangosd/logs"
echo "the log of ticket 12" > "$WOWAI_POOL_DIR/$s12/storage/mangosd/logs/Server.log"
run_kit gc
check "gc succeeds" test "$code" -eq 0
check "gc removes the lease of a deleted worktree" test ! -e "$WOWAI_POOL_DIR/$s12.lease"
check "gc keeps its Server.log first" \
  bash -c "grep -qs 'the log of ticket 12' '$WOWAI_POOL_DIR'/logs/t12/*/mangosd/Server.log"
check "gc stops that slot" grep -q "compose --project-name wowai-test-$s12 stop" "$FAKE_DOCKER_LOG"
check "gc stops that slot's dev container" grep -q " stop wowai-dev-slot-$s12$" "$FAKE_DOCKER_LOG"
check "gc keeps the leases of existing worktrees" test -n "$(slot_of 13)"

check "no command removes a slot" bash -c "! grep -E ' (down|rm)( |$)' '$FAKE_DOCKER_LOG'"
check "each Compose project is a slot or the baseline" bash -c \
  "! grep -oE -- '--project-name [^ ]+' '$FAKE_DOCKER_LOG' | grep -vE -- '--project-name wowai-test-([1-5]|baseline)$'"

# --- Core settings of a lease, with the real dev script ---

# A vmangos-deploy folder with an example mangosd.conf, extracted data, and a
# chat service: what `realm config` reads.
mkdir -p "$tmp/deploy/config" "$tmp/data/5875" "$WOWAI_BOTCHAT_SRC"
touch "$WOWAI_BOTCHAT_SRC/Dockerfile"
cat > "$tmp/deploy/config/mangosd.conf.example" <<'EOF'
WowPatch = 10
SOAP.Enabled = 0
Rate.XP.Kill = 1
Instance.PerHourLimit = 5
Instance.UnloadDelay = 1800000
LogLevel.File = 0
EOF
touch "$tmp/deploy/config/realmd.conf.example"
export WOWAI_DEPLOY_DIR="$tmp/deploy" WOWAI_REALM_DATA="$tmp/data"

WOWAI_KIT_DEV="$tmp/fake-dev" run_kit --ticket 21 acquire --set Rate.XP.Kill=3 --set 'Forever.DataDir="/opt/forever dbc"'
check "acquire with --set succeeds" test "$code" -eq 0
check "acquire does not give --set to the dev script" bash -c "! grep -q -- '--set' <<<'$out'"
s21="$(slot_of 21)"
conf="$WOWAI_POOL_DIR/$s21/config/mangosd.conf"
run_kit --ticket 21 config
check "config succeeds" test "$code" -eq 0
check "a setting of the lease that the file has gets its value" grep -qx 'Rate.XP.Kill = 3' "$conf"
check "a setting of the lease that the file lacks is added" grep -qx 'Forever.DataDir = "/opt/forever dbc"' "$conf"
check "the file has each setting once" test "$(grep -c '^Rate.XP.Kill' "$conf")" = 1
check "the scripted runs still get their instance limit" grep -qx 'Instance.PerHourLimit = 100000' "$conf"
check "the scripted runs still get their unload delay" grep -qx 'Instance.UnloadDelay = 120000' "$conf"
check "the kit still raises the file log level" grep -qx 'LogLevel.File = 3' "$conf"
check "SOAP is still on" grep -qx 'SOAP.Enabled = 1' "$conf"
inode="$(stat -c %i "$conf")"

run_kit --ticket 21 release
WOWAI_KIT_DEV="$tmp/fake-dev" run_kit --ticket 22 acquire
check "the next lease gets the same slot" test "$(slot_of 22)" = "$s21"
run_kit --ticket 22 config
check "a lease with no settings gets the default value" grep -qx 'Rate.XP.Kill = 1' "$conf"
check "a setting that the last lease added is gone" bash -c "! grep -q '^Forever.DataDir' '$conf'"
check "the scripted run settings stay with no lease settings" grep -qx 'Instance.PerHourLimit = 100000' "$conf"
check "the configuration file keeps its inode" test "$(stat -c %i "$conf")" = "$inode"

WOWAI_KIT_DEV="$tmp/fake-dev" run_kit --ticket 22 acquire --set Rate.XP.Kill=2
run_kit --ticket 22 config
check "a second acquire of a held lease writes its own settings" grep -qx 'Rate.XP.Kill = 2' "$conf"
WOWAI_KIT_DEV="$tmp/fake-dev" run_kit --ticket 22 acquire
run_kit --ticket 22 config
check "a second acquire with no settings gets the defaults" grep -qx 'Rate.XP.Kill = 1' "$conf"

for bad in 'Rate.XP.Kill' '=1' 'Bad Name=1' $'Rate.XP.Kill=a\nb'; do
  WOWAI_KIT_DEV="$tmp/fake-dev" run_kit --ticket 23 acquire --set "$bad"
  check "--set $(printf %q "$bad") is refused" test "$code" -ne 0
done
check "a refused request gets no lease" test -z "$(slot_of 23)"

finish
