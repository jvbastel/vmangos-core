# shellcheck shell=bash
# The test realm commands of the dev script. `wowai/dev` sources this file,
# so the functions of the dev script (cmd_build, die) work here.
#
#   wowai/dev realm up               Make the realm folder, apply the new
#                                    migrations, and start the test realm.
#                                    Waits for "World initialized.".
#   wowai/dev realm setup            Create the GM account again (`up` does it
#                                    too) and restart a running mangosd. Then
#                                    create the bots of the scripted runs.
#   wowai/dev realm build            Dev build, apply the new migrations, and
#                                    `docker compose restart mangosd`.
#   wowai/dev realm migrate          Apply each new file of sql/migrations/ once.
#   wowai/dev realm sql <db> <query> Run a query with the MariaDB client.
#   wowai/dev realm down [args]      `docker compose down` of the test realm.
#   wowai/dev realm compose <args>   Any other `docker compose` command.
#
# `up` (with no dev build yet) and `build` build into WOWAI_DEV_INSTALL. For
# the shared test realm in ~/wowai/dev-install, set WOWAI_DEV_SHARED_INSTALL=1.
# Both hold the lock of the dev container until they end.
#
# The realm folder holds the settings (.env), the configuration, and the logs.
# The Compose files stay in the fork. In the realm folder, `docker compose`
# also works by hand, because .env names the Compose files.
#
# Settings (environment variables, kept in .env after the first `up`):
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

# Wait for "World initialized." in the mangosd log since a time.
realm_wait_for_world() {
  local since="$1" container deadline=$((SECONDS + 900))
  echo "Waiting for mangosd to initialize the world..."
  while [ "$SECONDS" -lt "$deadline" ]; do
    container="$(realm_compose ps --quiet mangosd)"
    if [ -n "$container" ] && docker logs --since "$since" "$container" 2>&1 | grep -q 'World initialized\.'; then
      echo "mangosd is ready."
      return
    fi
    sleep 3
  done
  realm_compose logs --tail 40 mangosd >&2 || true
  die "mangosd did not initialize the world in 15 minutes"
}

realm_need_dev_install() {
  if [ ! -x "$install_dir/bin/mangosd" ] || [ ! -x "$install_dir/bin/realmd" ]; then
    echo "No dev build in $install_dir yet. Building it..."
    cmd_build
  fi
}

cmd_realm_up() {
  lock_container
  realm_prepare
  realm_need_dev_install
  local since
  since="$(date --utc +%Y-%m-%dT%H:%M:%SZ)"
  echo "Starting the database of the test realm $(realm_env COMPOSE_PROJECT_NAME) (a fresh one takes a few minutes)..."
  realm_compose up --detach --wait --wait-timeout 1800 database
  cmd_realm_migrate
  realm_setup_accounts
  realm_compose up --detach "$@"
  realm_wait_for_world "$since"
  realm_setup_bots
  echo "The test realm runs: login port $(realm_env WOWAI_REALM_LOGIN_PORT), world port $(realm_env WOWAI_REALM_WORLD_PORT), SOAP 127.0.0.1:$(realm_env WOWAI_REALM_SOAP_PORT)."
}

cmd_realm_build() {
  guard_shared_install
  lock_container
  realm_prepare
  cmd_build
  cmd_realm_migrate
  local since
  since="$(date --utc +%Y-%m-%dT%H:%M:%SZ)"
  realm_compose restart mangosd
  realm_wait_for_world "$since"
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
    sed -e 's/&#xD;//g' -e 's/.*<result>//' -e 's/<\/result>.*//' -e 's/.*<faultstring>//' -e 's/<\/faultstring>.*//'
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
  local recipe name i
  # SOAP answers a little after "World initialized.".
  for i in $(seq 1 30); do
    realm_soap 'server info' 2>/dev/null | grep -q 'Players online' && break
    sleep 2
  done
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
    local since
    since="$(date --utc +%Y-%m-%dT%H:%M:%SZ)"
    echo "Restarting mangosd, so that it reads the accounts again..."
    realm_compose restart mangosd
    realm_wait_for_world "$since"
    realm_setup_bots
  fi
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
    down) realm_compose down "$@" ;;
    compose) realm_compose "$@" ;;
    help|-h|--help) sed -n '2,/^$/p' "$realm_files/realm.sh" | sed 's/^# \{0,1\}//' ;;
    *) sed -n '2,/^$/p' "$realm_files/realm.sh" | sed 's/^# \{0,1\}//' >&2; exit 2 ;;
  esac
}
