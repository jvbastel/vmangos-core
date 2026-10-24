#!/usr/bin/env bash
# Writes the reference list of learnable spells that the GoogleTest test of
# the spell files (src/bots/tests/SpellDataTest.cpp) checks each spell ID
# against. It reads the world database of the test realm:
#
#   bots/tools/spells/export-learnable-spells.sh > src/bots/tests/data/learnable-spells.tsv
#
# A learnable spell is a spell that a "learn spell" or "learn pet spell"
# effect teaches (trainers, quests, books, and grimoires use these), or a
# starting spell of a character or a pet. Each line is
# `<spellId> TAB <name> TAB <rank or subtext>`, sorted by spell ID, from the
# newest client build of each spell (5875 = 1.12.1).
set -euo pipefail

dev="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)/wowai/dev"
sql() { "$dev" realm sql mangos "$1"; }

tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

sql "SELECT effect1, effectTriggerSpell1, effect2, effectTriggerSpell2, effect3, effectTriggerSpell3 FROM spell_template
     WHERE effect1 IN (36, 57) OR effect2 IN (36, 57) OR effect3 IN (36, 57);" \
  | awk '{ for (i = 1; i <= 5; i += 2) if ($i == 36 || $i == 57) print $(i + 1) }' > "$tmp/ids"
sql "SELECT Spell1 FROM petcreateinfo_spell UNION SELECT Spell2 FROM petcreateinfo_spell
     UNION SELECT Spell3 FROM petcreateinfo_spell UNION SELECT Spell4 FROM petcreateinfo_spell
     UNION SELECT Spell FROM playercreateinfo_spell;" >> "$tmp/ids"
sort -un "$tmp/ids" > "$tmp/learnable"

sql "SELECT entry, name, nameSubtext FROM spell_template s
     WHERE build = (SELECT MAX(build) FROM spell_template s2 WHERE s2.entry = s.entry) ORDER BY entry;" > "$tmp/all"

echo "# Learnable spells of the 1.12.1 world database. Made by bots/tools/spells/export-learnable-spells.sh."
awk -F'\t' 'NR == FNR { keep[$1]; next } ($1 in keep)' "$tmp/learnable" "$tmp/all"
