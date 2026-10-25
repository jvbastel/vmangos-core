#!/usr/bin/env bash
# shellcheck disable=SC2154,SC2016
# (wowai/dev sets script_dir, src, deploy_dir, and install_dir. The SQL in
# single quotes quotes its names in backticks.)
#
# The test realm commands, and the in-game check kit.
#
# Run as a command, this file is the check kit. It needs a ticket number. The
# ticket gives the realm its own Compose project, realm folder, dev install
# folder, and ports, so that two tickets never collide. It never falls back to
# the shared test realm: that needs --shared.
#
#   wowai/test-realm/realm.sh --ticket <N> <command> [args]
#   wowai/test-realm/realm.sh --shared <command> [args]   (no `down`)
#
# `wowai/dev realm <command>` sources this file and runs the same commands on
# the shared test realm (or on the realm that the WOWAI_REALM_* settings name).
#
#   up                   Make the realm folder, apply the new migrations, and
#                        start the realm. Builds the dev install if it has
#                        none. Waits until the world is ready.
#   wait                 Wait until mangosd runs GM commands over SOAP.
#   soap <command>       Run a GM command over SOAP. Prints the answer.
#   sql <db> <query>     Run a query with the MariaDB client.
#   restart              Keep a copy of Server.log, restart mangosd, wait,
#                        and log the bots that were online back in.
#   key <account>        Make a player account (GM level 6) if it has none,
#                        write a new session key, and print it (hex), for the
#                        Python client.
#   mark [label]         Mark the end of Server.log now.
#   log [--all]          The Server.log lines since the last mark, without
#                        the known noise (--all: with it).
#   check <script> [args]
#                        Mark the log, then run a Python check script with
#                        the kit's Python client on its path. Keeps a pid file.
#   env                  Print the settings that the ticket gives.
#   down                 Kit: stop the processes of the pid files, keep the
#                        logs, and remove the realm, its volume, and its
#                        folder. Shared: `docker compose down [args]`.
#   setup                Create the GM account again (`up` does it too) and
#                        restart a running mangosd. Then create the bots of
#                        the scripted runs.
#   build                Dev build, apply the new migrations, and restart
#                        mangosd.
#   migrate              Apply each new file of sql/migrations/ once.
#   compose <args>       Any other `docker compose` command.
#
# Kit folders (WOWAI_KIT_DIR, default ~/wowai/wt): the realm in
# t<N>-realm, the dev install in install-t<N>, and the logs that `down` keeps
# in t<N>-logs/<time>/. Ports: login 20000+N, world 21000+N, SOAP 22000+N.
# The kit raises LogLevel.File to 3, so the rules of the bots are in
# Server.log.
#
# The realm folder holds the settings (.env), the configuration, and the logs.
# The Compose files stay in the fork. In the realm folder, `docker compose`
# also works by hand, because .env names the Compose files.
#
# Settings of `wowai/dev realm` (environment variables, kept in .env after
# the first `up`). The kit sets the first five itself:
#   WOWAI_REALM_DIR         Realm folder. Default: ~/wowai/test-realm
#   WOWAI_REALM_PROJECT     Compose project name. Default: wowai-test-realm
#   WOWAI_REALM_LOGIN_PORT  realmd host port. Default: 3725
#   WOWAI_REALM_WORLD_PORT  mangosd host port. Default: 8086
#   WOWAI_REALM_SOAP_PORT   SOAP host port, on 127.0.0.1 only. Default: 7879
#   WOWAI_SERVER_IMAGE      Default: the newest local wowai/vmangos-server
#                           image, else ghcr.io/mserajnik/vmangos-server:5875
#   WOWAI_DATABASE_IMAGE    Default: the newest local wowai/vmangos-database
#                           image, else ghcr.io/mserajnik/vmangos-database:latest
#   WOWAI_REALM_DATA        Extracted client data (read-only).
#                           Default: <WOWAI_DEPLOY_DIR>/storage/mangosd/extracted-data
#   WOWAI_BOTCHAT_SRC       A clone of the chat service repository, which the
#                           botchat service runs. Default: ~/wowai/botchat
#   WOWAI_BOTS_DATA         The bot data files (gear templates, route files,
#                           ...), read and written by mangosd. Default:
#                           bots/data of the fork tree that runs `realm up`
#                           or `realm build`

# Run as a command: the check kit. Take the realm from the ticket number,
# then run the command through the dev script, which sources this file.
if [ "${BASH_SOURCE[0]}" = "$0" ]; then
  set -euo pipefail
  kit_die() { echo "realm.sh: $*" >&2; exit 1; }
  ticket=""
  case "${1:-}" in
    --ticket) [ "$#" -ge 2 ] || kit_die "--ticket needs a number"; ticket="$2"; shift 2 ;;
    --ticket=*) ticket="${1#--ticket=}"; shift ;;
    --shared) ticket=shared; shift ;;
    help|-h|--help) sed -n '6,/^$/p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'; exit 0 ;;
    *) kit_die "give --ticket <N>, the number of your ticket. The kit never falls back to the shared test realm; --shared names it." ;;
  esac
  [ "$#" -gt 0 ] || kit_die "give a command (see: realm.sh --help)"
  # Only the ticket names the realm.
  unset WOWAI_REALM_DIR WOWAI_REALM_PROJECT WOWAI_REALM_LOGIN_PORT WOWAI_REALM_WORLD_PORT \
    WOWAI_REALM_SOAP_PORT WOWAI_DEV_INSTALL WOWAI_REALM_KEPT_LOGS WOWAI_REALM_LOG_FILE_LEVEL
  if [ "$ticket" = shared ]; then
    [ "$1" != down ] || kit_die "the kit does not remove the shared test realm (wowai/dev realm down stops it)"
    export WOWAI_REALM_TICKET=shared
  else
    [[ "$ticket" =~ ^[1-9][0-9]{0,2}$ ]] || kit_die "the ticket number must be 1 to 999, not '$ticket'"
    kit_base="$(realpath -m "${WOWAI_KIT_DIR:-$HOME/wowai/wt}")"
    export WOWAI_REALM_TICKET="$ticket"
    export WOWAI_REALM_DIR="$kit_base/t$ticket-realm"
    export WOWAI_REALM_PROJECT="wowai-t$ticket-realm"
    export WOWAI_REALM_LOGIN_PORT=$((20000 + ticket))
    export WOWAI_REALM_WORLD_PORT=$((21000 + ticket))
    export WOWAI_REALM_SOAP_PORT=$((22000 + ticket))
    export WOWAI_DEV_INSTALL="$kit_base/install-t$ticket"
    export WOWAI_REALM_KEPT_LOGS="$kit_base/t$ticket-logs"
    export WOWAI_REALM_LOG_FILE_LEVEL=3
  fi
  exec "$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)/dev" realm "$@"
fi

realm_dir="$(realpath -m "${WOWAI_REALM_DIR:-$HOME/wowai/test-realm}")"
realm_files="$script_dir/test-realm"

# A setting: the environment first, then the .env of the realm folder, then
# the default.
realm_setting() {
  local name="$1" default="$2" saved
  if [ -n "${!name:-}" ]; then
    echo "${!name}"
    return
  fi
  saved="$(sed -n "s/^$name=//p" "$realm_dir/.env" 2>/dev/null | tail -n 1)"
  echo "${saved:-$default}"
}

newest_image() {
  local repository="$1" fallback="$2" newest
  newest="$(docker images --format '{{.Repository}}:{{.Tag}}' "$repository" | grep -v ':<none>$' | head -n 1 || true)"
  echo "${newest:-$fallback}"
}

random_secret() { tr -dc 'A-Z0-9' < /dev/urandom | head -c "${1:-20}" || true; }

# Write the .env of the realm folder and the configuration files.
realm_prepare() {
  mkdir -p "$realm_dir/config" "$realm_dir/storage/mangosd/logs" \
    "$realm_dir/storage/mangosd/honor" "$realm_dir/storage/realmd/logs"
  mkdir -p "$install_dir"
  install_dir="$(cd "$install_dir" && pwd)"
  local bots_data
  bots_data="$(realpath -m "${WOWAI_BOTS_DATA:-$src/bots/data}")"
  # The GM route recorder writes into routes/. Made here, so that it belongs
  # to the user and not to root.
  mkdir -p "$bots_data/routes"

  local data
  data="$(realm_setting WOWAI_REALM_DATA "$deploy_dir/storage/mangosd/extracted-data")"
  [ -d "$data/5875" ] || die "no extracted client data in $data (set WOWAI_REALM_DATA)"
  local botchat
  botchat="$(realpath -m "$(realm_setting WOWAI_BOTCHAT_SRC "$HOME/wowai/botchat")")"
  [ -f "$botchat/Dockerfile" ] || die "no chat service in $botchat (clone jvbastel/botchat there, or set WOWAI_BOTCHAT_SRC)"

  local env
  env="$(cat <<EOF
# Written by \`wowai/dev realm\`. Change a value here or set it in the
# environment. The passwords stay for the life of the database volume.
COMPOSE_PROJECT_NAME=$(realm_setting WOWAI_REALM_PROJECT wowai-test-realm)
COMPOSE_FILE=$realm_files/compose.yaml:$realm_files/compose.dev.yaml
WOWAI_REALM_PROJECT=$(realm_setting WOWAI_REALM_PROJECT wowai-test-realm)
WOWAI_REALM_DIR=$realm_dir
WOWAI_DEV_INSTALL=$install_dir
WOWAI_REALM_DATA=$data
WOWAI_BOTCHAT_SRC=$botchat
WOWAI_BOTS_DATA=$bots_data
WOWAI_REALM_LOGIN_PORT=$(realm_setting WOWAI_REALM_LOGIN_PORT 3725)
WOWAI_REALM_WORLD_PORT=$(realm_setting WOWAI_REALM_WORLD_PORT 8086)
WOWAI_REALM_SOAP_PORT=$(realm_setting WOWAI_REALM_SOAP_PORT 7879)
WOWAI_SERVER_IMAGE=${WOWAI_SERVER_IMAGE:-$(newest_image wowai/vmangos-server ghcr.io/mserajnik/vmangos-server:5875)}
WOWAI_DATABASE_IMAGE=$(realm_setting WOWAI_DATABASE_IMAGE "$(newest_image wowai/vmangos-database ghcr.io/mserajnik/vmangos-database:latest)")
WOWAI_REALM_TZ=$(realm_setting WOWAI_REALM_TZ "$(cat /etc/timezone 2>/dev/null || echo Etc/UTC)")
WOWAI_REALM_UID=$(id -u)
WOWAI_REALM_GID=$(id -g)
WOWAI_REALM_DB_PASSWORD=$(realm_setting WOWAI_REALM_DB_PASSWORD "$(random_secret 24)")
WOWAI_REALM_DB_ROOT_PASSWORD=$(realm_setting WOWAI_REALM_DB_ROOT_PASSWORD "$(random_secret 24)")
WOWAI_REALM_BOTCHAT_DB_PASSWORD=$(realm_setting WOWAI_REALM_BOTCHAT_DB_PASSWORD "$(random_secret 24)")
WOWAI_REALM_GM_ACCOUNT=$(realm_setting WOWAI_REALM_GM_ACCOUNT GM)
WOWAI_REALM_GM_PASSWORD=$(realm_setting WOWAI_REALM_GM_PASSWORD "$(random_secret 12)")
EOF
)"
  (umask 077 && printf '%s\n' "$env" > "$realm_dir/.env")

  # The configuration files of vmangos-deploy, with the play realm's game
  # settings and SOAP on. Made once; later edits stay.
  local example="$deploy_dir/config"
  if [ ! -f "$realm_dir/config/mangosd.conf" ]; then
    tr -d '\r' < "$example/mangosd.conf.example" | sed \
      -e 's/^WowPatch = .*/WowPatch = 0/' \
      -e 's/^GameType = .*/GameType = 0/' \
      -e 's/^RealmZone = .*/RealmZone = 2/' \
      -e 's/^SOAP.Enabled = .*/SOAP.Enabled = 1/' \
      > "$realm_dir/config/mangosd.conf"
  fi
  # The settings that the scripted runs need, also in an older file: each
  # run enters a new instance of the dungeon.
  sed -i -e 's/^Instance.PerHourLimit = .*/Instance.PerHourLimit = 100000/' \
    -e 's/^Instance.UnloadDelay = .*/Instance.UnloadDelay = 120000/' "$realm_dir/config/mangosd.conf"
  # The kit: the DETAIL lines (the rules that the bots do) in Server.log.
  if [ -n "${WOWAI_REALM_LOG_FILE_LEVEL:-}" ]; then
    sed -i "s/^LogLevel.File = .*/LogLevel.File = $WOWAI_REALM_LOG_FILE_LEVEL/" "$realm_dir/config/mangosd.conf"
  fi
  if [ ! -f "$realm_dir/config/realmd.conf" ]; then
    tr -d '\r' < "$example/realmd.conf.example" > "$realm_dir/config/realmd.conf"
  fi
}

realm_env() { sed -n "s/^$1=//p" "$realm_dir/.env" | tail -n 1; }

realm_compose() {
  [ -f "$realm_dir/.env" ] || die "no test realm in $realm_dir yet (run: wowai/dev realm up)"
  docker compose --project-directory "$realm_dir" --env-file "$realm_dir/.env" \
    --file "$realm_files/compose.yaml" --file "$realm_files/compose.dev.yaml" "$@"
}

# Arguments: the database, then the MariaDB client options (for example -e
# '<query>'). Standard input goes to the client.
realm_mariadb() {
  local db="$1"; shift
  realm_compose exec -T database sh -c \
    'db="$1"; shift; exec mariadb -u root -p"$MARIADB_ROOT_PASSWORD" --batch --skip-column-names "$db" "$@"' \
    mariadb "$db" "$@"
}

cmd_realm_sql() {
  [ "$#" -eq 2 ] || die "usage: wowai/dev realm sql <database> <query>"
  realm_mariadb "$1" -e "$2"
}

# Apply each file of sql/migrations/ once. A file ran when its timestamp is
# in the `migrations` table of its database. The upstream files add it
# themselves; for other files the script adds it.
cmd_realm_migrate() {
  local -A db_of=([world]=mangos [characters]=characters [logon]=realmd [logs]=logs)
  local -A done_ids=()
  local kind id file name applied=0 skipped=0
  for kind in "${!db_of[@]}"; do
    while read -r id; do
      done_ids["$kind:$id"]=1
    done < <(realm_mariadb "${db_of[$kind]}" -e 'SELECT `id` FROM `migrations`' | tr -d '\r')
  done

  for file in "$src"/sql/migrations/*_*.sql; do
    [ -e "$file" ] || continue
    name="$(basename "$file")"
    id="${name%%_*}"
    kind="${name#*_}"; kind="${kind%.sql}"
    [ -n "${db_of[$kind]:-}" ] || continue
    if [ -n "${done_ids["$kind:$id"]:-}" ]; then
      skipped=$((skipped + 1))
      continue
    fi
    echo "Applying the migration $name to the ${db_of[$kind]} database..."
    realm_mariadb "${db_of[$kind]}" < "$file" || die "the migration $name failed"
    realm_mariadb "${db_of[$kind]}" -e "INSERT IGNORE INTO \`migrations\` (\`id\`) VALUES ('$id')"
    applied=$((applied + 1))
  done
  echo "Migrations: $applied applied, $skipped already applied."
}

# Wait until mangosd runs a GM command over SOAP: then the world is ready.
# The answer goes into a variable first. A pipe into `grep -q` fails under
# pipefail when grep stops reading early.
realm_wait_ready() {
  local deadline=$((SECONDS + ${1:-900})) answer
  echo "Waiting until mangosd runs GM commands..."
  while [ "$SECONDS" -lt "$deadline" ]; do
    [ -n "$(realm_compose ps --all --quiet mangosd)" ] || die "the realm has no mangosd container (run: up)"
    answer="$(realm_soap 'server info' 2>/dev/null || true)"
    case "$answer" in
      *'Players online'*) echo "mangosd is ready."; return 0 ;;
    esac
    sleep 2
  done
  realm_compose logs --tail 40 mangosd >&2 || true
  die "mangosd did not get ready in ${1:-900} seconds"
}

realm_need_dev_install() {
  if [ ! -x "$install_dir/bin/mangosd" ] || [ ! -x "$install_dir/bin/realmd" ]; then
    echo "No dev build in $install_dir yet. Building it..."
    cmd_build
  fi
}

cmd_realm_up() {
  realm_prepare
  realm_need_dev_install
  echo "Starting the database of the test realm $(realm_env COMPOSE_PROJECT_NAME) (a fresh one takes a few minutes)..."
  realm_compose up --detach --wait --wait-timeout 1800 database
  cmd_realm_migrate
  realm_setup_accounts
  realm_compose up --detach "$@"
  realm_wait_ready
  realm_setup_bots
  echo "The test realm runs: login port $(realm_env WOWAI_REALM_LOGIN_PORT), world port $(realm_env WOWAI_REALM_WORLD_PORT), SOAP 127.0.0.1:$(realm_env WOWAI_REALM_SOAP_PORT)."
}

cmd_realm_build() {
  realm_prepare
  cmd_build
  cmd_realm_migrate
  realm_compose restart mangosd
  realm_wait_ready
}

# The verifier and salt of an account password, as AccountMgr makes them
# (SRP6 of the WoW 1.12 login). Arguments: the account name and password.
srp6_verifier() {
  python3 -I - "$1" "$2" <<'PY'
import hashlib, os, sys
N = int("894B645E89E1535BBDAD5B8B290650530801B18EBFBF5E8FAB3C82872A3E9BB7", 16)
user, password = sys.argv[1].upper(), sys.argv[2].upper()
digest = hashlib.sha1(f"{user}:{password}".encode()).digest()
while True:
    salt = int.from_bytes(os.urandom(32), "big")
    if salt >> 248:
        break
x = int.from_bytes(hashlib.sha1(salt.to_bytes(32, "little") + digest).digest(), "little")
print(format(salt, "X"), format(pow(7, x, N), "X"))
PY
}

# Create the GM account (level 6 on all realms), or set its password again.
# `realm up` runs it before mangosd starts.
realm_setup_accounts() {
  local account password salt verifier
  account="$(realm_env WOWAI_REALM_GM_ACCOUNT | tr '[:lower:]' '[:upper:]')"
  password="$(realm_env WOWAI_REALM_GM_PASSWORD)"
  [[ "$account" =~ ^[A-Z0-9]+$ ]] || die "the GM account name must have only letters and digits"
  read -r salt verifier < <(srp6_verifier "$account" "$password")
  realm_mariadb realmd <<SQL
INSERT INTO \`account\` (\`username\`, \`s\`, \`v\`, \`joindate\`)
  VALUES ('$account', '$salt', '$verifier', NOW())
  ON DUPLICATE KEY UPDATE \`s\` = VALUES(\`s\`), \`v\` = VALUES(\`v\`);
REPLACE INTO \`account_access\` (\`id\`, \`gmlevel\`, \`RealmID\`)
  SELECT \`id\`, 6, -1 FROM \`account\` WHERE \`username\` = '$account';
INSERT IGNORE INTO \`realmcharacters\` (\`realmid\`, \`acctid\`, \`numchars\`)
  SELECT r.\`id\`, a.\`id\`, 0 FROM \`realmlist\` r, \`account\` a WHERE a.\`username\` = '$account';
SQL
  echo "GM account: $account, password: $password (in $realm_dir/.env)."
}

# A GM command over SOAP. Prints the answer of mangosd.
realm_soap() {
  local account password
  account="$(realm_env WOWAI_REALM_GM_ACCOUNT)"
  password="$(realm_env WOWAI_REALM_GM_PASSWORD)"
  curl --silent --show-error --max-time 30 --user "$account:$password" --header 'Content-Type: text/xml' \
    --data "<?xml version=\"1.0\" encoding=\"utf-8\"?><SOAP-ENV:Envelope xmlns:SOAP-ENV=\"http://schemas.xmlsoap.org/soap/envelope/\" xmlns:ns1=\"urn:MaNGOS\"><SOAP-ENV:Body><ns1:executeCommand><command>$1</command></ns1:executeCommand></SOAP-ENV:Body></SOAP-ENV:Envelope>" \
    "http://127.0.0.1:$(realm_env WOWAI_REALM_SOAP_PORT)/" | tr '\n' ' ' |
    sed -e 's/&#xD;//g' -e 's/<result\/>/<result><\/result>/' -e 's/.*<result>//' -e 's/<\/result>.*//' -e 's/.*<faultstring>//' -e 's/<\/faultstring>.*//'
  echo
}

# The bots of the scripted runs (bots/data/bottests/*.txt name them): the
# four version 1 bots and the test driver, a frost Mage with a fixed gear
# template. `.wowai bot create` makes the bot accounts too. A bot that
# exists already stays as it is.
realm_bots=(
  "Grukk warrior tank"
  "Hela priest healer"
  "Shanka rogue melee"
  "Morzul warlock ranged"
  "Ezzra mage ranged troll gear test_driver_mage"
)

realm_setup_bots() {
  local recipe name
  for recipe in "${realm_bots[@]}"; do
    name="${recipe%% *}"
    if [ -n "$(realm_mariadb characters -e "SELECT 1 FROM \`characters\` WHERE \`name\` = '$name'")" ]; then
      echo "Bot $name: exists."
      continue
    fi
    echo "Bot $name: $(realm_soap ".wowai bot create $recipe")"
    sleep 1
  done
}

# mangosd reads the GM levels of the accounts only at startup.
cmd_realm_setup() {
  realm_compose up --detach --wait --wait-timeout 1800 database
  realm_setup_accounts
  if [ -n "$(realm_compose ps --quiet --status running mangosd)" ]; then
    echo "Restarting mangosd, so that it reads the accounts again..."
    realm_compose restart mangosd
    realm_wait_ready
    realm_setup_bots
  fi
}

# mangosd restarts with no bots online: log the bots back in that were online.
cmd_realm_restart() {
  local bots name
  bots="$(realm_mariadb characters -e 'SELECT c.`name` FROM `characters` c JOIN `bot_character` b ON b.`guid` = c.`guid` WHERE c.`online` = 1' | tr -d '\r')"
  realm_keep_server_log
  realm_compose restart mangosd
  realm_wait_ready
  for name in $bots; do
    echo "Bot $name: $(realm_soap ".wowai bot login $name")"
  done
}

# A player account for the Python client, which skips realmd: GM level 6,
# and a new session key that mangosd checks against the client's. Prints the
# key as hex (big-endian, as BigNumber::SetHexStr reads it).
cmd_realm_key() {
  [ "$#" -eq 1 ] || die "usage: realm key <account>"
  local account key gateway
  account="$(tr '[:lower:]' '[:upper:]' <<<"$1")"
  [[ "$account" =~ ^[A-Z0-9]+$ ]] || die "the account name must have only letters and digits"
  key="$(python3 -I -c 'import os; k = bytearray(os.urandom(40)); k[0] |= 0x80; print(k.hex().upper())')"
  # mangosd sees the client at the gateway of the realm network.
  gateway="$(docker network inspect "$(realm_env COMPOSE_PROJECT_NAME)_default" --format '{{(index .IPAM.Config 0).Gateway}}')"
  realm_mariadb realmd <<SQL
INSERT IGNORE INTO \`account\` (\`username\`, \`v\`, \`s\`, \`joindate\`) VALUES ('$account', '00', '00', NOW());
REPLACE INTO \`account_access\` (\`id\`, \`gmlevel\`, \`RealmID\`)
  SELECT \`id\`, 6, -1 FROM \`account\` WHERE \`username\` = '$account';
INSERT IGNORE INTO \`realmcharacters\` (\`realmid\`, \`acctid\`, \`numchars\`)
  SELECT r.\`id\`, a.\`id\`, 0 FROM \`realmlist\` r, \`account\` a WHERE a.\`username\` = '$account';
UPDATE \`account\` SET \`sessionkey\` = '$key', \`last_ip\` = '$gateway', \`last_login\` = NOW(),
  \`os\` = 'Win', \`platform\` = 'x86' WHERE \`username\` = '$account';
SQL
  echo "$key"
}

realm_server_log() {
  local log="$realm_dir/storage/mangosd/logs/Server.log"
  [ -f "$log" ] || die "no $log yet (run: up)"
  echo "$log"
}

# The mark is the size of Server.log, in the file log-mark of the realm
# folder. A line appended to Server.log would not last: mangosd opens it
# with "w" and writes over it at its own offset. mangosd also empties
# Server.log at each start, so `restart` keeps a copy first.
cmd_realm_mark() {
  local log
  log="$(realm_server_log)"
  stat --format %s "$log" > "$realm_dir/log-mark"
  echo "Mark: ${*:-mark} (Server.log at byte $(cat "$realm_dir/log-mark"))."
}

# The lines of Server.log that are known noise for a check: the waypoint
# paths of each map grid that loads, each SOAP connection (one for each kit
# command), and the failed navmesh casts of random moves.
realm_log_noise=(
  '^[0-9-]+ [0-9:]+ LoadPath: loading waypoint path '
  '^[0-9-]+ [0-9:]+ MaNGOSsoap: Accepted connection '
  '^[0-9-]+ [0-9:]+ WalkHitPos: '
  '^[0-9-]+ [0-9:]+ Server Population '
)

cmd_realm_log() {
  local log all=0 noise from=0 size
  log="$(realm_server_log)"
  case "${1:-}" in
    --all) all=1 ;;
    '') ;;
    *) die "usage: realm log [--all]" ;;
  esac
  [ ! -f "$realm_dir/log-mark" ] || from="$(cat "$realm_dir/log-mark")"
  size="$(stat --format %s "$log")"
  # A smaller file: mangosd started again since the mark.
  [ "$size" -ge "$from" ] || from=0
  noise="$(IFS='|'; echo "${realm_log_noise[*]}")"
  tail --bytes "+$((from + 1))" "$log" | tr -d '\r' |
    awk -v all="$all" -v noise="$noise" 'all || $0 !~ noise'
}

# Keep a copy of Server.log before mangosd starts again and empties it.
realm_keep_server_log() {
  local log="$realm_dir/storage/mangosd/logs/Server.log" keep
  [ -n "${WOWAI_REALM_KEPT_LOGS:-}" ] && [ -f "$log" ] || return 0
  keep="$WOWAI_REALM_KEPT_LOGS/$(date '+%Y%m%d-%H%M%S')-restart"
  mkdir -p "$keep"
  cp -a "$log" "$keep/Server.log"
  echo "Kept Server.log in $keep."
}

# The start time of a process (field 22 of /proc/<pid>/stat), so that a pid
# file does not match a later process with the same pid.
realm_proc_start() {
  sed 's/.*) //' "/proc/$1/stat" 2>/dev/null | cut -d' ' -f20
}

# Run a Python check with the kit's client on its path. The realm settings
# are in the environment, so the script can call the kit back.
cmd_realm_check() {
  [ "$#" -ge 1 ] || die "usage: realm check <script> [args]"
  [ -f "$1" ] || die "no check script $1"
  local pid_dir="$realm_dir/pids" pid status=0
  [ -f "$realm_dir/.env" ] || die "no realm in $realm_dir yet (run: up)"
  mkdir -p "$pid_dir"
  cmd_realm_mark "check $(basename "$1")"
  WOWAI_REALM_CLI="$script_dir/dev" PYTHONPATH="$realm_files/check${PYTHONPATH:+:$PYTHONPATH}" \
    python3 -u "$@" &
  pid=$!
  echo "$pid $(realm_proc_start "$pid")" > "$pid_dir/$pid.pid"
  wait "$pid" || status=$?
  rm -f "$pid_dir/$pid.pid"
  return "$status"
}

# Stop the checks of the pid files: only the kit's own processes.
realm_stop_checks() {
  local file pid start
  for file in "$realm_dir"/pids/*.pid; do
    [ -e "$file" ] || continue
    read -r pid start < "$file" || true
    if [ -n "$pid" ] && [ -n "$start" ] && [ "$(realm_proc_start "$pid")" = "$start" ]; then
      echo "Stopping the check $pid..."
      kill "$pid" 2>/dev/null || true
    fi
    rm -f "$file"
  done
}

# The kit: keep the logs, then remove the realm, its volume, and its folder.
# The servers write as the user (VMANGOS_UID), so no file needs root.
cmd_realm_kit_down() {
  local keep project
  project="$(realm_setting WOWAI_REALM_PROJECT '')"
  [ -n "$project" ] && [ "$project" != wowai-test-realm ] || die "down removes only the realm of a ticket"
  realm_stop_checks
  keep="${WOWAI_REALM_KEPT_LOGS:?}/$(date '+%Y%m%d-%H%M%S')"
  if [ -f "$realm_dir/.env" ]; then
    mkdir -p "$keep"
    realm_compose logs --no-color > "$keep/compose.log" 2>&1 || true
    realm_compose down --volumes --remove-orphans
  else
    docker compose --project-name "$project" down --volumes --remove-orphans 2>/dev/null || true
  fi
  if [ -d "$realm_dir/storage" ]; then
    mkdir -p "$keep"
    cp -a "$realm_dir/storage/mangosd/logs" "$keep/mangosd" 2>/dev/null || true
    cp -a "$realm_dir/storage/realmd/logs" "$keep/realmd" 2>/dev/null || true
  fi
  if [ -d "$realm_dir" ] && ! rm -rf "$realm_dir"; then
    die "could not remove $realm_dir; files of another user: $(find "$realm_dir" ! -user "$(id -u)" | head -n 5 | tr '\n' ' ')"
  fi
  if [ -d "$keep" ]; then
    echo "Removed the realm $project. Kept its logs in $keep."
  else
    echo "Removed the realm $project."
  fi
}

cmd_realm_down() {
  case "${WOWAI_REALM_TICKET:-}" in
    ''|shared) realm_compose down "$@" ;;
    *) cmd_realm_kit_down ;;
  esac
}

# The settings that name the realm.
cmd_realm_env() {
  local name
  echo "WOWAI_REALM_TICKET=${WOWAI_REALM_TICKET:-}"
  echo "WOWAI_REALM_DIR=$realm_dir"
  echo "WOWAI_REALM_PROJECT=$(realm_setting WOWAI_REALM_PROJECT wowai-test-realm)"
  for name in LOGIN:3725 WORLD:8086 SOAP:7879; do
    echo "WOWAI_REALM_${name%%:*}_PORT=$(realm_setting "WOWAI_REALM_${name%%:*}_PORT" "${name#*:}")"
  done
  echo "WOWAI_DEV_INSTALL=$(realpath -m "$install_dir")"
}

cmd_realm() {
  local command="${1:-help}"
  [ "$#" -eq 0 ] || shift
  case "$command" in
    up) cmd_realm_up "$@" ;;
    setup) cmd_realm_setup "$@" ;;
    build) cmd_realm_build "$@" ;;
    migrate) cmd_realm_migrate "$@" ;;
    sql) cmd_realm_sql "$@" ;;
    down) cmd_realm_down "$@" ;;
    wait) realm_wait_ready "$@" ;;
    soap) [ "$#" -gt 0 ] || die "usage: realm soap <command>"; realm_soap "$*" ;;
    restart) cmd_realm_restart ;;
    key) cmd_realm_key "$@" ;;
    mark) cmd_realm_mark "$@" ;;
    log) cmd_realm_log "$@" ;;
    check) cmd_realm_check "$@" ;;
    compose) realm_compose "$@" ;;
    env) cmd_realm_env ;;
    help|-h|--help) sed -n '6,/^$/p' "$realm_files/realm.sh" | sed 's/^# \{0,1\}//' ;;
    *) sed -n '6,/^$/p' "$realm_files/realm.sh" | sed 's/^# \{0,1\}//' >&2; exit 2 ;;
  esac
}
