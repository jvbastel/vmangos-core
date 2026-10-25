#!/usr/bin/env bash
# Tests of `wowai/release/release-core`, with a fake `docker` and local
# repositories in place of GitHub. It builds no image and touches no realm.
# It takes a few seconds. Run it from any folder:
#
#   wowai/release/tests/release-core.test.sh
# The checks run `bash -c '... "$0" "$1"' <args>`, so the single quotes are meant.
# shellcheck disable=SC2016
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
release="$here/../release-core"

tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

failures=0
pass() { echo "PASS: $*"; }
fail() { echo "FAIL: $*"; failures=$((failures + 1)); }
check() { local name="$1"; shift; if "$@"; then pass "$name"; else fail "$name"; fi; }
logged() { grep -qF -- "$1" "$FAKE_DOCKER_LOG"; }
not_logged() { ! grep -qF -- "$1" "$FAKE_DOCKER_LOG"; }

git_quiet() { git -c core.autocrlf=false -c user.name=t -c user.email=t@t "$@" >/dev/null 2>&1; }

# The fake docker.
mkdir -p "$tmp/bin"
ln -s "$here/fake-docker" "$tmp/bin/docker"
export PATH="$tmp/bin:$PATH"
export FAKE_DOCKER_LOG="$tmp/docker.log" FAKE_DOCKER_IMAGES="$tmp/images"
reset_docker_log() { : >"$FAKE_DOCKER_LOG"; }

# A fake fork with an `origin` and an `upstream`. Upstream has one migration.
# The fork adds one of its own.
git_quiet init --bare -b development "$tmp/upstream.git"
git_quiet init --bare -b wowai "$tmp/fork.git"
git_quiet clone "$tmp/upstream.git" "$tmp/fork"
mkdir -p "$tmp/fork/sql/migrations"
echo "-- upstream" >"$tmp/fork/sql/migrations/20260101000000_world.sql"
git_quiet -C "$tmp/fork" add -A
git_quiet -C "$tmp/fork" commit -m "Upstream start"
git_quiet -C "$tmp/fork" push origin HEAD:development
upstream_commit="$(git -C "$tmp/fork" rev-parse HEAD)"
git_quiet -C "$tmp/fork" remote rename origin upstream
git_quiet -C "$tmp/fork" remote add origin "$tmp/fork.git"
git_quiet -C "$tmp/fork" fetch upstream
git_quiet -C "$tmp/fork" checkout -b wowai
echo "-- ours" >"$tmp/fork/sql/migrations/20261001000000_characters.sql"
mkdir -p "$tmp/fork/bots/data/gear" "$tmp/fork/bots/tools/gear"
echo "template warrior_tank warrior tank generated" >"$tmp/fork/bots/data/gear/warrior_tank.txt"
echo "print('a tool')" >"$tmp/fork/bots/tools/gear/generate.py"
git_quiet -C "$tmp/fork" add -A
git_quiet -C "$tmp/fork" commit -m "Our migration"
pushed="$(git -C "$tmp/fork" rev-parse HEAD)"
echo "fix" >"$tmp/fork/fix.cpp"
git_quiet -C "$tmp/fork" add -A
git_quiet -C "$tmp/fork" commit -m "A fix with no migration"
git_quiet -C "$tmp/fork" push origin wowai
pushed2="$(git -C "$tmp/fork" rev-parse HEAD)"
echo "code" >"$tmp/fork/code.cpp"
git_quiet -C "$tmp/fork" add -A
git_quiet -C "$tmp/fork" commit -m "Not pushed"
unpushed="$(git -C "$tmp/fork" rev-parse HEAD)"

# A fake vmangos-deploy repository, with LF files.
git_quiet init -b master "$tmp/deploy-src"
mkdir -p "$tmp/deploy-src/docker/server" "$tmp/deploy-src/docker/database" "$tmp/deploy-src/docker/patches"
echo "FROM scratch AS build" >"$tmp/deploy-src/docker/server/Dockerfile"
echo "FROM scratch AS build" >"$tmp/deploy-src/docker/database/Dockerfile"
printf -- '--- a/x\n+++ b/x\n' >"$tmp/deploy-src/docker/patches/one.patch"
git_quiet -C "$tmp/deploy-src" add -A
git_quiet -C "$tmp/deploy-src" commit -m "Deploy"
deploy_pin="$(git -C "$tmp/deploy-src" rev-parse HEAD)"
echo "later" >"$tmp/deploy-src/later.txt"
git_quiet -C "$tmp/deploy-src" add -A
git_quiet -C "$tmp/deploy-src" commit -m "Later deploy commit"

export WOWAI_SRC="$tmp/fork"
export WOWAI_FORK_URL="https://example.invalid/fork.git"
export WOWAI_DEPLOY_URL="$tmp/deploy-src"
export WOWAI_DEPLOY_PIN="$deploy_pin"
export WOWAI_DEPLOY_BUILD_DIR="$tmp/deploy-build"

# --- build ---

reset_docker_log
check "build refuses a branch name" \
  bash -c '! "$0" build wowai >/dev/null 2>&1' "$release"
check "build refuses a short commit" \
  bash -c '! "$0" build "${1:0:12}" >/dev/null 2>&1' "$release" "$pushed"
check "build refuses a commit that is not pushed to origin" \
  bash -c '! "$0" build "$1" >/dev/null 2>&1' "$release" "$unpushed"
check "a refused build runs no docker build" not_logged " build "

reset_docker_log
server="wowai/vmangos-server:5875-$pushed"
database="wowai/vmangos-database:$pushed"
check "build of a pushed full commit works" "$release" build "$pushed"
check "the deploy clone is at the pinned commit" \
  test "$(git -C "$WOWAI_DEPLOY_BUILD_DIR" rev-parse HEAD)" = "$deploy_pin"
check "the deploy clone has core.autocrlf=false" \
  test "$(git -C "$WOWAI_DEPLOY_BUILD_DIR" config core.autocrlf)" = false
check "the server image builds from the deploy clone's server Dockerfile" \
  logged "$WOWAI_DEPLOY_BUILD_DIR | build --file docker/server/Dockerfile"
check "the database image builds from the deploy clone's database Dockerfile" \
  logged "$WOWAI_DEPLOY_BUILD_DIR | build --file docker/database/Dockerfile"
check "both builds use the fork URL" \
  test "$(grep -cF -- "--build-arg VMANGOS_REPOSITORY_URL=$WOWAI_FORK_URL " "$FAKE_DOCKER_LOG")" = 2
check "both builds use the full commit" \
  test "$(grep -cF -- "--build-arg VMANGOS_REVISION=$pushed " "$FAKE_DOCKER_LOG")" = 2
check "both builds fail on a patch error" \
  test "$(grep -cF -- "--build-arg VMANGOS_FAIL_ON_PATCH_ERROR=1 " "$FAKE_DOCKER_LOG")" = 2
check "the server image is tagged with the commit" docker image inspect --format "{{.Id}}" "$server"
stock="wowai/vmangos-server-stock:5875-$pushed"
check "the stock server image gets its own tag" \
  logged "$WOWAI_DEPLOY_BUILD_DIR | build --file docker/server/Dockerfile --build-arg VMANGOS_CLIENT_VERSION=5875"
check "the stock server build is tagged as the stock image" \
  bash -c 'grep -F docker/server/Dockerfile "$0" | grep -qF -- "--tag $1 "' "$FAKE_DOCKER_LOG" "$stock"
check "the server image adds bots/data on top of the stock image" \
  bash -c 'grep -F -- "--build-arg BASE_IMAGE=$1 " "$0" | grep -qF -- "--tag $2 "' "$FAKE_DOCKER_LOG" "$stock" "$server"
check "the bots/data build context holds the commit's bots/data" \
  logged "context: bots/data/gear/warrior_tank.txt"
check "the bots/data build context holds no other files of the fork" \
  not_logged "context: bots/tools/gear/generate.py"
check "the bots/data build has the revision label" \
  bash -c 'grep -F -- "--build-arg BASE_IMAGE=" "$0" | grep -qF -- "--label org.opencontainers.image.revision=$1"' "$FAKE_DOCKER_LOG" "$pushed"
check "the bots/data Dockerfile copies bots/data to the working folder of mangosd" \
  grep -qx 'COPY bots/data/ /home/vmangos/bots/' "$here/../bots-data.Dockerfile"
check "the database image is tagged with the commit" docker image inspect --format "{{.Id}}" "$database"

reset_docker_log
"$release" build --migration-edits "world:core@abc|characters:core@def" "$pushed" >/dev/null
check "the database build gets VMANGOS_MIGRATION_EDITS" \
  bash -c 'grep -F docker/database/Dockerfile "$0" | grep -qF -- "--build-arg VMANGOS_MIGRATION_EDITS=world:core@abc|characters:core@def "' "$FAKE_DOCKER_LOG"

# --- up ---

# A fake play realm: the vmangos-deploy example Compose file with the backup
# service on, with CRLF endings as Git on Windows writes it. Its images are
# the published ones of the upstream commit.
play="$tmp/play"
export WOWAI_PLAY_REALM="$play"
mkdir -p "$play/storage/database/backups"
sed 's/$/\r/' "$here/play-compose.yaml" >"$play/compose.yaml"
cp "$play/compose.yaml" "$tmp/compose.before"
fake_image() { printf '%s\n' "$2" >"$FAKE_DOCKER_IMAGES/$(printf '%s' "$1" | tr '/:' '__')"; }
fake_image ghcr.io/mserajnik/vmangos-database "$upstream_commit"
fake_image ghcr.io/mserajnik/vmangos-server:5875 "$upstream_commit"

# The Compose file without its image and pull_policy lines.
other_lines() { grep -vE '^    (image|pull_policy):' "$1"; }
service_block() { tr -d '\r' <"$play/compose.yaml" | sed -n "/^  $1:/,/^  [a-z#]/p"; }

reset_docker_log
check "up refuses a commit that has no images" \
  bash -c '! "$0" up "$1" >/dev/null 2>&1' "$release" "$unpushed"
reset_docker_log
check "up stops when the backup fails" \
  bash -c '! FAKE_DOCKER_BACKUP_FAILS=1 "$0" up "$1" >/dev/null 2>&1' "$release" "$pushed"
check "a failed up leaves the Compose file as it was" cmp -s "$play/compose.yaml" "$tmp/compose.before"
check "a failed up starts nothing" not_logged "compose up"

reset_docker_log
check "up of a built commit works" "$release" up "$pushed"
check "up makes a backup with the backup-now command" \
  logged "$play | compose run --rm -e DB_DUMP_CRON= -e DB_DUMP_ONCE=true database-backup"
check "the backup comes before the start" \
  test "$(grep -n 'DB_DUMP_ONCE' "$FAKE_DOCKER_LOG" | cut -d: -f1)" -lt "$(grep -n 'compose up -d' "$FAKE_DOCKER_LOG" | cut -d: -f1)"
check "up runs docker compose up -d in the play realm" logged "$play | compose up -d"
check "database gets the new database image" \
  bash -c 'grep -qx "    image: $1" <<<"$0"' "$(service_block database)" "$database"
check "realmd gets the new server image" \
  bash -c 'grep -qx "    image: $1" <<<"$0"' "$(service_block realmd)" "$server"
check "mangosd gets the new server image and keeps its anchor" \
  bash -c 'grep -qx "    image: &mangosd-image $1" <<<"$0"' "$(service_block mangosd)" "$server"
for service in database realmd mangosd; do
  check "$service has pull_policy: never once" \
    test "$(service_block "$service" | grep -c '^    pull_policy: never$')" = 1
done
check "the other lines of the Compose file stay" \
  bash -c 'diff <(grep -vE "^    (image|pull_policy):" "$0") <(grep -vE "^    (image|pull_policy):" "$1")' \
  "$tmp/compose.before" "$play/compose.yaml"
check "the Compose file keeps its CRLF endings" \
  test "$(grep -c $'\r$' "$play/compose.yaml")" = "$(wc -l <"$play/compose.yaml")"
check "up removes no image" bash -c '! grep -E " (rmi|image rm|image prune|system prune)" "$0"' "$FAKE_DOCKER_LOG"
first_backup="$(ls "$play/storage/database/backups")"

# --- rollback ---

# A second release with no new migration of our own.
"$release" build "$pushed2" >/dev/null
cp "$play/compose.yaml" "$tmp/compose.first"
"$release" up "$pushed2" >/dev/null 2>&1
reset_docker_log
check "rollback of a release without a migration of our own works" "$release" rollback
check "it sets the previous release's tags back" cmp -s "$play/compose.yaml" "$tmp/compose.first"
check "it restores no backup" not_logged "restore"
check "it runs docker compose up -d" logged "$play | compose up -d"

reset_docker_log
check "rollback of the first release, which ran a migration of our own, works" "$release" rollback
check "it sets the published images back, as the file was" cmp -s "$play/compose.yaml" "$tmp/compose.before"
check "it stops the servers" logged "$play | compose stop realmd mangosd"
check "it restores the backup made before that release" \
  logged "$play | compose run --rm database-backup restore --target /backup $first_backup"
check "the servers stop before the restore, and start after it" \
  bash -c 'grep -n . "$0" | grep -E "compose (stop|run --rm database-backup restore|up -d)" | cut -d"|" -f2 | tr -d "\n" | grep -q "compose stop.*restore.*compose up -d"' "$FAKE_DOCKER_LOG"
check "rollback removes no image" bash -c '! grep -E " (rmi|image rm|image prune|system prune)" "$0"' "$FAKE_DOCKER_LOG"

check "a rollback with no release left fails" bash -c '! "$0" rollback >/dev/null 2>&1' "$release"

echo
if [ "$failures" -eq 0 ]; then echo "All checks passed."; else echo "$failures check(s) failed."; exit 1; fi
