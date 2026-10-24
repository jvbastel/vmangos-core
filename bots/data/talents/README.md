# Talent builds

A talent build is the order in which a bot of one class and role spends its
talent points to level 60. mangosd reads every `*.txt` file of this folder at
startup (`<Bots.DataDir>/talents/`). See step 4 of the version 1 build spec.

The full rules are in `src/bots/TalentBuild.h`. In short:

```
# Comments start with # and run to the end of the line.
build warrior_tank warrior tank
pick 1601 1    # Shield Specialization
pick 1601 2    # Shield Specialization
pick 1601 3    # Shield Specialization
pick 1601 4    # Shield Specialization
pick 1601 5    # Shield Specialization
pick 140 1     # Toughness
```

- `build <name> <class> <role>` is the first line. The name is the file name
  without `.txt`. The level routine gives a bot the build named
  `<class>_<role>`, for example `warrior_tank.txt` or `mage_ranged.txt`.
- `pick <talentId> <rank>`: one line per talent point. The first pick is the
  point of level 10, so a character at level L has the first L minus 9 picks,
  and a build has at most 51 picks. `<talentId>` is the ID in Talent.dbc, not
  a spell ID. `<rank>` is the rank of the talent after this point, from 1.
  The comment is the talent name.
- The talent IDs, trees, tiers, ranks, prerequisites, and names of 1.12.1 are
  in `src/bots/tests/data/talents.tsv`.
- mangosd at startup and the GoogleTest test `TalentData` reject a build that
  breaks a talent rule: a talent of another class, a rank out of order or
  above the maximum rank, a tier before 5 points per tier in its tree, or a
  talent before its prerequisite. The test also checks that each comment is
  the talent name. Run it with `wowai/dev test`.

The level routine spends the picks with `Player::LearnTalent` at creation,
at each login, and at each level-up. If the bot has a talent rank that its
first L minus 9 picks do not have (more picks than L minus 9, for example
after a GM level-down, or a talent off the build), it resets the talents
with `ResetTalents(true)` first.
