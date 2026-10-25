# Coding standards (WOWAI bot work)

These rules are for the reviewer of a ticket diff, and for the agent that writes it.
They hold only judgement rules. CI checks the mechanical rules (the core file
allowlist and debug lines, shellcheck, the bot code tests). Do not copy a CI rule here.

Each rule gives the reason. The examples come from the version 1 run.

## 1. The handback tells the whole truth

- A handback lists every check that failed or was skipped, with the reason.
- Code with a known failing check is not handed back as done.
- "Passed in 2 of 4 runs" is a failing check. Say so, and say what you know about the cause.

Why: in version 1 a flaky headless check (#36) was handed back as done. The cause
(an account race on back-to-back creates) was found three tickets later.

## 2. A known bug is fixed or written down

- A fact that an explore note (`~/wowai/notes/explore-*.md`) marks as a bug is fixed
  in the same ticket, or the handback lists it as a follow-up.
- The same holds for a bug that you find while you work on something else.

Why: the far teleport that nothing acked (#25) was in an explore note, but no ticket
owned it until #48 hit it again.

## 3. Shared bot code has one owner

- Targeting, range, and movement helpers each have one owning module:
  - Target pickers: `src/bots/CommonRules.h` and `src/bots/GroupRules.h`.
  - Chase and range: `BotActions::ChaseToRange` and `ChaseBehind`
    (`src/bots/BotActions.h`, real code in `src/game/Bots/BotAdapter.cpp`).
  - Game queries: `src/bots/BotView.h`.
- Before you write a helper, search for one that does the same job.
  If one exists, extend it. Do not write a second copy.
- A new helper that other class modules can use goes into the shared module,
  not into one class module.

Why: #36 built `ChaseInRange` and #38 built `ChaseToRange` at the same time.

## 4. A game state change says how it reaches the database and the client

A change to saving, the online flag, death, revive, or teleport says, in a code
comment or the handback:

- how the change reaches the database (which save, and when), and
- how it reaches the client, or why no client sees it (a bot has no client).

Then check both for real: read the row in the database, and watch the bot in game.

Why: in #25 bot saving was off and the online flag stayed at 1. A teleport to
another map waited for an `SMSG_NEW_WORLD` answer that no bot sends.

## 5. Rules check the bot's state before they act

A rule that casts, attacks, or revives checks the state that stops it:

- Seated, dead, or a ghost.
- In combat, for the bot or its group (no revive in the middle of a fight).
- In range and in reach (spell range, melee reach).
- The resource it needs (mana, energy, combo points).
- A spell rank that the target can take (a low-level target needs a low rank).

Each such rule has a unit test for the case that must stop it.

Why: a seated bot cast spells (#37). A dead tank revived next to the boss while the
others still fought (#42). The Rogue misjudged combo points and melee reach (#36).

## 6. Shared resources are safe when two calls come at once

Two bots or two commands can ask for the same account, guid, name, or file at
the same time. Give these out under a lock, or in one database step.
Test two back-to-back calls.

Why: two quick bot creates took the same account (#25, #31).

## 7. The release ships what the dev build uses

The server can read a new data folder, config key, or file at runtime. Add it
to the release image and to the release docs in the same ticket.
Check it in the release image, not only in the dev install.

Why: the release image did not ship `bots/data`, so the gear templates were missing (#31).

## 8. Stay close to upstream

- Bot logic goes into `src/bots` (no game headers, with GoogleTest tests, ADR 0002).
- Game glue goes into `src/game/Bots`.
- A change to another core file is a hook that the spec names. If it is not in the
  spec, the handback says why it is needed. Keep the change as small as you can.

Why: #36 added `GetAngle()` to `TargetedMovementGenerator.h`, a core file outside the spec.
