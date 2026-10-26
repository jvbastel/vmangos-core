#!/usr/bin/env bash
# shellcheck disable=SC2016 # (The backticks in single quotes are Markdown and SQL, not commands.)
# Test of the game layer map: wowai/docs/game-layer-map.md.
#
# The map lists paths, database columns, and GM commands. This test checks
# them, so that the map cannot go out of date without a failure.
#
#   wowai/tests/game-layer-map.test.sh               Paths only. No Docker.
#   wowai/tests/game-layer-map.test.sh --realm <N>   Also the columns and the
#                                                    GM commands, on the
#                                                    running check realm of
#                                                    ticket N (realm.sh).
#
# What the test reads from the map:
#   - Paths: each `code` span that starts with src/, sql/, bots/, wowai/, or
#     dep/ and has no space, < >, or * in it (a command is not a path). The path must exist in the tree.
#   - Columns: each row of a table with the header | Database | Table |
#     Columns |. The test selects the columns from the table (LIMIT 0).
#   - GM commands: each row of a table with the header | Command | Example
#     | Answer has |. The test runs the example over SOAP, in the order of the
#     map, and the answer must contain the text of the third cell.
# Use a newly acquired slot (`realm.sh --ticket <N> acquire`): the examples
# use the five bots that `acquire` makes.
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo="$(cd "$here/../.." && pwd)"
doc="$repo/wowai/docs/game-layer-map.md"
realm="$repo/wowai/test-realm/realm.sh"

ticket=""
case "${1:-}" in
  "") ;;
  --realm) [ "$#" -eq 2 ] || { echo "usage: $0 [--realm <ticket>]" >&2; exit 2; }; ticket="$2" ;;
  *) echo "usage: $0 [--realm <ticket>]" >&2; exit 2 ;;
esac

# shellcheck source=lib.sh
. "$here/lib.sh"
# The map has hundreds of checks: print only the failures.
quiet_pass=1

[ -f "$doc" ] || { echo "FAIL: no map at $doc"; exit 1; }

# The cells of a table row: "| a | b | c |" -> lines a, b, c (trimmed).
cells() { sed -e 's/^ *|//' -e 's/| *$//' <<<"$1" | tr '|' '\n' | sed -e 's/^ *//' -e 's/ *$//'; }
# Drop the backticks of a cell.
plain() { tr -d '`' <<<"$1"; }

# The rows of the table whose header row starts with the given cells.
table_rows() {
  awk -v head="$1" '
    index($0, head) == 1 { inside = 1; getline; next }
    inside && /^\|/ { print; next }
    inside { inside = 0 }
  ' "$doc"
}

# 1. Paths.
paths=0
while IFS= read -r path; do
  paths=$((paths + 1))
  if [ -e "$repo/$path" ]; then pass; else fail "path $path is not in the tree"; fi
done < <(grep -oE '`(src|sql|bots|wowai|dep)/[^`<>* ]*`' "$doc" | tr -d '`' | sed 's/[.:,]$//' | sort -u)
[ "$paths" -ge 20 ] || fail "the map names only $paths paths (expected at least 20)"
echo "Paths: $paths checked."

if [ -z "$ticket" ]; then
  [ "$failures" -eq 0 ] || { echo "$failures of $checked checks failed."; exit 1; }
  echo "All $checked checks passed (paths only; --realm <N> checks the columns and GM commands)."
  exit 0
fi

# 2. Columns.
tables=0
while IFS= read -r row; do
  mapfile -t c < <(cells "$row")
  db="$(plain "${c[0]}")"; table="$(plain "${c[1]}")"
  columns="$(plain "${c[2]}" | sed 's/ *, */`, `/g')"
  tables=$((tables + 1))
  out="$("$realm" --ticket "$ticket" sql "$db" "SELECT \`$columns\` FROM \`$table\` LIMIT 0" </dev/null 2>&1)" ||
    { fail "columns of $db.$table: $out"; continue; }
  pass
done < <(table_rows "| Database | Table | Columns |")
[ "$tables" -ge 5 ] || fail "the map lists only $tables tables (expected at least 5)"
echo "Tables: $tables checked."

# 3. GM commands over SOAP.
commands=0
while IFS= read -r row; do
  mapfile -t c < <(cells "$row")
  example="$(plain "${c[1]}")"; want="$(plain "${c[2]}")"
  commands=$((commands + 1))
  answer="$("$realm" --ticket "$ticket" soap "$example" </dev/null 2>&1)" || true
  if [[ "$answer" == *"$want"* ]]; then pass; else fail "SOAP '$example': want '$want', got '$answer'"; fi
done < <(table_rows "| Command | Example | Answer has |")
[ "$commands" -ge 10 ] || fail "the map lists only $commands GM commands (expected at least 10)"
echo "GM commands: $commands checked."

[ "$failures" -eq 0 ] || { echo "$failures of $checked checks failed."; exit 1; }
echo "All $checked checks passed."
