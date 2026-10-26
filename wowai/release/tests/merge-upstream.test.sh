#!/usr/bin/env bash
# Tests of `wowai/release/merge-upstream`, in a throwaway clone whose
# `origin` and `upstream` are local bare repositories. Nothing reaches GitHub.
# It takes a few seconds. Run it from any folder:
#
#   wowai/release/tests/merge-upstream.test.sh
# The checks run `bash -c '... "$0" "$1"' <args>`, so the single quotes are meant.
# shellcheck disable=SC2016
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
merge="$here/../merge-upstream"

tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

# shellcheck source=../../tests/lib.sh
. "$here/../../tests/lib.sh"

export GIT_AUTHOR_NAME=t GIT_AUTHOR_EMAIL=t@t GIT_COMMITTER_NAME=t GIT_COMMITTER_EMAIL=t@t
g() { git -c core.autocrlf=false "$@" >/dev/null 2>&1; }
commit() { g -C "$1" add -A && g -C "$1" commit -m "$2" && git -C "$1" rev-parse HEAD; }

# Upstream, with old migrations of each database.
g init --bare -b development "$tmp/upstream.git"
g init --bare -b wowai "$tmp/origin.git"
up="$tmp/upstream-work"
g clone "$tmp/upstream.git" "$up"
m="$up/sql/migrations"
mkdir -p "$m"
echo "a" >"$m/20250101000000_world.sql"
echo "b" >"$m/20250102000000_characters.sql"
echo "c" >"$m/20250103000000_logon.sql"
printf 'd\nd\nd\nd\n' >"$m/20250104000000_world.sql"
echo "readme" >"$m/README"
commit "$up" "Upstream start" >/dev/null
g -C "$up" push origin HEAD:development

# The fork: wowai branches off upstream and adds its own work.
fork="$tmp/fork"
g clone -o upstream "$tmp/upstream.git" "$fork"
g -C "$fork" remote add origin "$tmp/origin.git"
g -C "$fork" checkout -b wowai
echo "ours" >"$fork/sql/migrations/20261001000000_characters.sql"
echo "bot" >"$fork/bot.cpp"
commit "$fork" "Our work" >/dev/null
g -C "$fork" push origin wowai
origin_before="$(git -C "$tmp/origin.git" for-each-ref)"

# New upstream commits: edits, a rename, and a delete of old migrations, and
# one new migration.
echo "a2" >>"$m/20250101000000_world.sql"; c1="$(commit "$up" "Edit an old world migration")"
g -C "$up" mv sql/migrations/20250104000000_world.sql sql/migrations/20250105000000_world.sql
c2="$(commit "$up" "Rename an old world migration")"
g -C "$up" rm -q sql/migrations/20250103000000_logon.sql; c3="$(commit "$up" "Delete an old logon migration")"
echo "e" >"$m/20261010000000_world.sql"; commit "$up" "A new migration" >/dev/null
echo "b2" >>"$m/20250102000000_characters.sql"; c5="$(commit "$up" "Edit an old characters migration")"
echo "a3" >>"$m/20250101000000_world.sql"; c6="$(commit "$up" "Edit the old world migration again")"
g -C "$up" push origin HEAD:development
upstream_tip="$(git -C "$up" rev-parse HEAD)"
upstream_before="$(git -C "$tmp/upstream.git" for-each-ref)"

export WOWAI_SRC="$fork"

# Refusals.
echo "dirty" >>"$fork/bot.cpp"
check "it refuses a tree with local changes" bash -c '! "$0" >/dev/null 2>&1' "$merge"
g -C "$fork" checkout bot.cpp
g -C "$fork" checkout -b feature
check "it refuses a branch other than wowai" bash -c '! "$0" >/dev/null 2>&1' "$merge"
g -C "$fork" checkout wowai

head_before="$(git -C "$fork" rev-parse HEAD)"
out="$tmp/out.txt"
check "it merges upstream development" bash -c '"$0" >"$1" 2>&1' "$merge" "$out"
sed 's/^/    | /' "$out"

check "a merge, not a rebase: MERGE_HEAD is the upstream tip" \
  test "$(git -C "$fork" rev-parse -q --verify MERGE_HEAD)" = "$upstream_tip"
check "it stops for review: nothing is committed yet" test "$(git -C "$fork" rev-parse HEAD)" = "$head_before"
check "the merged files are staged" test -f "$fork/sql/migrations/20261010000000_world.sql"
for file in 20250101000000_world.sql 20250102000000_characters.sql 20250103000000_logon.sql 20250104000000_world.sql; do
  check "it lists the edited old migration $file" grep -q "sql/migrations/$file" "$out"
done
check "it does not list a new upstream migration" bash -c '! grep -q 20261010000000_world.sql "$0"' "$out"
check "it does not list our own migration" bash -c '! grep -q 20261001000000_characters.sql "$0"' "$out"
check "it prints the VMANGOS_MIGRATION_EDITS value" \
  grep -qx "VMANGOS_MIGRATION_EDITS='world:core@$c1,core@$c2,core@$c6|characters:core@$c5|realmd:core@$c3'" "$out"
check "nothing was pushed to origin" test "$(git -C "$tmp/origin.git" for-each-ref)" = "$origin_before"
check "nothing was pushed to upstream" test "$(git -C "$tmp/upstream.git" for-each-ref)" = "$upstream_before"

check "it refuses to start while a merge is in progress" bash -c '! "$0" >/dev/null 2>&1' "$merge"

echo
finish
