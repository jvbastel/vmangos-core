#!/usr/bin/env bash
# End-to-end test of the test realm: `wowai/dev realm ...`.
#
# It clones the fork into a temporary tree and brings up a test realm from
# that tree with its own Compose project, volume, ports, and dev install. At
# the end it removes them again. It does not touch the default test realm.
# A run takes several minutes (a first dev build and a fresh database).
#
#   wowai/tests/test-realm.test.sh
#
# Keep the temporary realm for a look by hand: WOWAI_TEST_KEEP=1.
# The botchat service runs WOWAI_BOTCHAT_SRC (default ~/wowai/botchat).
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
dev="$here/../dev"
repo="$(git -C "$here" rev-parse --show-toplevel)"

tmp="$(mktemp -d)"
export WOWAI_SRC="$tmp/src"
export WOWAI_DEV_INSTALL="$tmp/install"
export WOWAI_REALM_DIR="$tmp/realm"
export WOWAI_REALM_PROJECT="wowai-test-realm-check-$$"
export WOWAI_REALM_LOGIN_PORT=3736
export WOWAI_REALM_WORLD_PORT=8096
export WOWAI_REALM_SOAP_PORT=7889

failures=0
pass() { echo "PASS: $*"; }
fail() { echo "FAIL: $*"; failures=$((failures + 1)); }
check() { local name="$1"; shift; if "$@"; then pass "$name"; else fail "$name"; fi; }

cleanup() {
  if [ "${WOWAI_TEST_KEEP:-0}" = "1" ]; then
    echo "Kept the realm $WOWAI_REALM_PROJECT in $tmp."
    return
  fi
  "$dev" realm down --volumes >/dev/null 2>&1 || true
  "$dev" clean >/dev/null 2>&1 || true
  # The realm folders can have files of the container users.
  docker run --rm --mount "type=bind,source=$tmp,target=/tmp-realm" alpine:latest \
    rm -rf /tmp-realm/realm /tmp-realm/install >/dev/null 2>&1 || true
  rm -rf "$tmp"
}
trap cleanup EXIT

git clone --quiet --shared "$repo" "$WOWAI_SRC"
git -C "$WOWAI_SRC" config core.autocrlf false

realm_sql() { "$dev" realm sql "$@" | tr -d '\r'; }
soap() {
  curl --silent --max-time 10 --user "$1" \
    --header 'Content-Type: text/xml' \
    --data "<?xml version=\"1.0\" encoding=\"utf-8\"?><SOAP-ENV:Envelope xmlns:SOAP-ENV=\"http://schemas.xmlsoap.org/soap/envelope/\" xmlns:ns1=\"urn:MaNGOS\"><SOAP-ENV:Body><ns1:executeCommand><command>$2</command></ns1:executeCommand></SOAP-ENV:Body></SOAP-ENV:Envelope>" \
    "http://127.0.0.1:$WOWAI_REALM_SOAP_PORT/"
}
mangosd_container() { docker ps --quiet --filter "label=com.docker.compose.project=$WOWAI_REALM_PROJECT" --filter "label=com.docker.compose.service=mangosd"; }
mangosd_exe_sha() { docker exec --user "$(id -u)" "$(mangosd_container)" sh -c 'sha256sum /proc/$(pidof mangosd)/exe' | cut -d' ' -f1; }

echo "Bringing up the test realm..."
"$dev" realm up

# Its own Compose project, database volume, and ports.
check "the realm is its own Compose project" \
  bash -c "docker compose ls --all --format json | grep -q '\"$WOWAI_REALM_PROJECT\"'"
volume="$(docker volume ls --quiet --filter "label=com.docker.compose.project=$WOWAI_REALM_PROJECT")"
check "the realm has its own database volume ($volume)" \
  bash -c "[ -n '$volume' ] && [ '$volume' != vmangos-deploy_vmangos-database ]"
ports="$(docker port "$(mangosd_container)")"
check "mangosd has the world port $WOWAI_REALM_WORLD_PORT" grep -q "^8085/tcp -> 0.0.0.0:$WOWAI_REALM_WORLD_PORT$" <<<"$ports"
check "SOAP is published on localhost only" \
  bash -c "[ \"\$(grep -c '^7878/tcp' <<<'$ports')\" = 1 ] && grep -q '^7878/tcp -> 127.0.0.1:$WOWAI_REALM_SOAP_PORT$' <<<'$ports'"
realmd="$(docker ps --quiet --filter "label=com.docker.compose.project=$WOWAI_REALM_PROJECT" --filter "label=com.docker.compose.service=realmd")"
check "realmd has the login port $WOWAI_REALM_LOGIN_PORT" \
  bash -c "docker port '$realmd' | grep -q '^3724/tcp -> 0.0.0.0:$WOWAI_REALM_LOGIN_PORT$'"
check "the server list entry has the world port" \
  test "$(realm_sql realmd "SELECT port FROM realmlist WHERE id = 1")" = "$WOWAI_REALM_WORLD_PORT"

# The dev override.
check "the override mounts the dev install folder over /opt/vmangos/bin" \
  bash -c "docker inspect --format '{{range .Mounts}}{{.Source}}={{.Destination}} {{end}}' '$(mangosd_container)' | grep -q '$WOWAI_DEV_INSTALL/bin=/opt/vmangos/bin '"
check "mangosd runs the dev build" \
  test "$(mangosd_exe_sha)" = "$(sha256sum "$WOWAI_DEV_INSTALL/bin/mangosd" | cut -d' ' -f1)"

# The chat service, from the realm network, as the core reaches it.
botchat_is_fake() {
  docker run --rm --network "${WOWAI_REALM_PROJECT}_default" alpine:latest \
    wget -qO- http://botchat:8000/health | grep -q '"llm":"fake"'
}
check "the botchat service answers /health with the fake LLM backend" botchat_is_fake

# The botchat database: botchat.sql of the chat service, then its migrations.
check "the database service creates the botchat database with botchat.sql" \
  test "$(realm_sql mysql "SELECT COUNT(*) FROM information_schema.schemata WHERE schema_name = 'botchat'")" = 1
check "the botchat service applies its migrations at start" \
  test "$(realm_sql botchat "SELECT COUNT(*) FROM botchat_migrations")" = 1
botchat_user_sql() {
  # shellcheck disable=SC2016 # The container shell expands these.
  "$dev" realm compose exec -T database sh -c \
    'mariadb -u botchat -p"$BOTCHAT_DB_PASSWORD" --batch --skip-column-names -e "$1"' sh "$1" 2>&1 | tr -d '\r'
}
check "the botchat user sees only the botchat database" \
  test "$(botchat_user_sql "SHOW DATABASES" | tr '\n' ' ')" = "botchat information_schema "

# The GM account, made by `realm up`.
gm="$(sed -n 's/^WOWAI_REALM_GM_ACCOUNT=//p' "$WOWAI_REALM_DIR/.env"):$(sed -n 's/^WOWAI_REALM_GM_PASSWORD=//p' "$WOWAI_REALM_DIR/.env")"
check "the setup creates a GM account with level 6" \
  test "$(realm_sql realmd "SELECT aa.gmlevel FROM account a JOIN account_access aa ON aa.id = a.id WHERE a.username = '${gm%%:*}'")" = 6
soap_runs() { soap "$1" 'server info' | grep -q 'executeCommandResponse'; }
check "the GM account logs in to SOAP and runs a command" soap_runs "$gm"
soap_refuses() { ! soap_runs "$1"; }
check "a wrong password does not log in to SOAP" soap_refuses "${gm%%:*}:wrong"
"$dev" realm setup
check "a second setup run keeps one GM account" \
  test "$(realm_sql realmd "SELECT COUNT(*) FROM account WHERE username = '${gm%%:*}'")" = 1

# A new migration applies once.
migration="$WOWAI_SRC/sql/migrations/29990101000000_characters.sql"
cat > "$migration" <<'SQL'
CREATE TABLE IF NOT EXISTS `wowai_migration_check` (`n` INT NOT NULL);
INSERT INTO `wowai_migration_check` VALUES (1);
SQL
"$dev" realm migrate
check "a new migration file applies" \
  test "$(realm_sql characters "SELECT COUNT(*) FROM wowai_migration_check")" = 1
second_run="$("$dev" realm migrate)"
check "a second run does not apply it again" \
  test "$(realm_sql characters "SELECT COUNT(*) FROM wowai_migration_check")" = 1
check "a second run applies nothing" grep -q "^Migrations: 0 applied" <<<"$second_run"
check "the migration is recorded in the migrations table" \
  test "$(realm_sql characters "SELECT COUNT(*) FROM migrations WHERE id = '29990101000000'")" = 1
rm "$migration"

# Build and restart.
old_sha="$(mangosd_exe_sha)"
sed -i 's/World server is running realm ID/WOWAI-T24-MARKER World server is running realm ID/' "$WOWAI_SRC/src/mangosd/Master.cpp"
since="$(date --utc +%Y-%m-%dT%H:%M:%SZ)"
"$dev" realm build
new_sha="$(mangosd_exe_sha)"
check "the build changes the mangosd binary" test "$old_sha" != "$new_sha"
check "mangosd runs the new dev build" \
  test "$new_sha" = "$(sha256sum "$WOWAI_DEV_INSTALL/bin/mangosd" | cut -d' ' -f1)"
check "the restarted mangosd logs the change" \
  bash -c "docker logs --since '$since' '$(mangosd_container)' 2>&1 | grep -q WOWAI-T24-MARKER"
check "the restarted mangosd initializes the world" \
  bash -c "docker logs --since '$since' '$(mangosd_container)' 2>&1 | grep -q 'World initialized.'"

if [ "$failures" -gt 0 ]; then
  echo "$failures check(s) failed."
  exit 1
fi
echo "All checks passed."
