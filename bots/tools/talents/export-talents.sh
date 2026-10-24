#!/usr/bin/env bash
# Writes the talent table that the GoogleTest tests of the talent builds
# (src/bots/tests/TalentDataTest.cpp) check each build against:
#
#   bots/tools/talents/export-talents.sh <dbc folder> > src/bots/tests/data/talents.tsv
#
# <dbc folder> holds the Talent.dbc and TalentTab.dbc files of the 1.12.1
# client, for example `<extracted-data>/5875/dbc`. The talent names are the
# names of the rank 1 spells, read from the world database of the test realm.
# Each line is `<talentId> <class> <tree> <tier> <column> <maxRank>
# <requiredTalent> <requiredRank> <name>`, split by tabs. The tree is the
# name of the talent tree in lower case with `-` for spaces, for example
# `protection` or `beast-mastery`. Tier, column, and the required rank count
# from 1; requiredTalent is 0 when there is none.
set -euo pipefail

dbc="${1:?usage: export-talents.sh <dbc folder>}"
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
dev="$here/../../../wowai/dev"

tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

"$dev" realm sql mangos "SELECT entry, name FROM spell_template s
    WHERE build = (SELECT MAX(build) FROM spell_template s2 WHERE s2.entry = s.entry);" > "$tmp/names"
python3 -I "$here/export_talents.py" "$dbc" "$tmp/names"
