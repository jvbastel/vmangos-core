#!/usr/bin/env bash
# Runs shellcheck on each shell script under wowai/. WOWAI CI runs it, and
# `wowai/dev shell` has shellcheck for a run by hand:
#
#   wowai/shellcheck.sh
#
# A shell script is a file of git under wowai/ that ends in .sh, starts with
# a sh or bash shebang, or has a `# shellcheck shell=` line (a sourced file).
# So a new script needs no edit here. wowai/.shellcheckrc holds the settings.
set -euo pipefail

repo="$(git -C "$(dirname "${BASH_SOURCE[0]}")" rev-parse --show-toplevel)"
cd "$repo"

scripts=()
while IFS= read -r -d '' file; do
  [ -f "$file" ] || continue
  if [[ "$file" == *.sh ]] ||
    head -n 1 "$file" | grep -qE '^#!.*[/ ](ba)?sh([[:space:]]|$)' ||
    grep -qE '^# shellcheck shell=' "$file"; then
    scripts+=("$file")
  fi
done < <(git ls-files -z -- wowai)

[ "${#scripts[@]}" -gt 0 ] || { echo "found no shell script under wowai/" >&2; exit 1; }
printf 'shellcheck %s\n' "${scripts[@]}"
shellcheck "${scripts[@]}"
echo "shellcheck found nothing in ${#scripts[@]} scripts."
