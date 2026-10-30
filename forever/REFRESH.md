# Refresh of the Forever spec

The spec is the folder `forever/spec`, one file per area. The generator in `forever/generator` reads every
`*.tsv` file in it and makes all Forever data from them. Nobody edits the generated files by hand.

- `warrior.tsv` holds the Forever rules of the Warrior (spec jvbastel/WOWAI#204): one row per talent, per
  new ability and per changed base ability.
- `blacksmithing.tsv` holds the Blacksmithing recipes (spec #225): one row per new recipe spell.
- `items.tsv` holds the new and changed items (spec #225): one row per item.

Each header names the sources, the Forever build, the date, its free ID range, the next free IDs and each
column. Within a file, the rows of one ticket stay in a block under a comment line that names the ticket.

## Free ID ranges

| IDs | Range | Named in | Notes |
|---|---|---|---|
| Spells, talents, skill line abilities | 60000-65535 | `warrior.tsv` | spell IDs are SMALLINT UNSIGNED in the world database |
| Profession spells (recipes) | 61000-61999 | `blacksmithing.tsv` | a block of the spell range; the core can tell a profession spell from a Warrior spell by its ID, even with the rules off. The generator refuses a recipe outside it |
| Items | 70000-79999 | `items.tsv` | the stock 1.12 world database ends at item 25818. A changed stock item keeps its own ID |

A new row takes the next free ID of its range, and the header then moves "Next free" on. Never use again an
ID that an older spec gave out: a character can still know the spell or own the item.

A refresh is a pull request that shows the changed rows. Nothing changes on a server until the player
merges it.

## 1. Gather the data again

1. Find the newest Forever build. Read the build reports of wow-forever.gg (`/news/`): they list the spell
   and talent changes from one build to the next. If a report says 0 Warrior changes, change only the build
   and the date in the header.
2. Layout and behaviour text: read the talent page and the class changes page of the two fan sources in the
   header (classicwowforever.com and wow-forever.gg). Use sources that read the build data, not guides.
   Read Blizzard's development notes since the last refresh for behaviour that the data does not show.
3. Numbers: if the local Forever client is installed, read its talent and spell tables again. The lead's
   notes tell how (`notes/forever/client-extract-research.md`). The dump tools stay outside every repo.
   Only numbers and names go into the spec, never icons, textures or other client files.
4. When the sources disagree, the build data wins over Blizzard's notes, and the in-game tooltip wins
   over a calculator. Write each disagreement and its choice in the header.

## 2. Change the rows

- Position, ranks and prerequisite: `tree`, `tier`, `column`, `ranks`, `requiredTalent`, `requiredRank`.
  Update the talent count line of the header.
- A surviving talent keeps its 1.12 talent ID, and each rank keeps the stock spell of that rank. The row
  changes that spell in place (`spells` = `sourceSpells`).
- A rank without a stock spell, a new talent or a new ability takes the next free ID from the header. Then
  move the "Next free IDs" of the header on. Never use again an ID that an older spec gave out, because a
  character can still know it.
- Values: `effect1` to `effect3` hold the raw values per rank (Rage in tenths, time in ms). Other spell columns
  go into `set` (the list is `FIELDS` in `generator/forever_gen/spec.py`). An effect without an effect type
  holds a value for the tooltip or for server code only.
- `port`: data (the spell data is enough), C++ (server code is necessary) or substitute (a workaround).
  The `notes` say what changed from stock and which ticket owns the code.
- Out of scope, never rows: Expertise, the racial changes, Legacy perks, the "Talented" points, Dual
  Specialization.
- A recipe (`blacksmithing.tsv`) is a copy of a stock recipe spell (`sourceSpell`), which keeps the stock
  cast time, tool and icon. Pick a stock recipe of the same profession and station. A recipe whose stock
  focus is the Anvil keeps the Anvil, even where Forever asks for a Master Forge.
- An item (`items.tsv`) is a copy of the stock row of `sourceItem` (class, subclass, slot, material,
  durability). Pick a stock item of the same kind. Base stats go into `stats`; every other stat becomes a
  stock equip spell in `equipSpells` (for example 9395 for +5 spell damage and healing). Keep the Forever
  values in `foreverStats`. `displayId` is a stock `ItemDisplayInfo.dbc` ID, which also gives the icon.

## 3. Pick a stock icon

A new talent or spell takes a stock 1.12 icon, an ID of `SpellIcon.dbc` of the stock client data. List the
names with the stock DBC folder of the server (`<extracted-data>/5875/dbc/SpellIcon.dbc`, 2 fields: ID and
`Interface\Icons\...` path) and pick an icon whose name fits, for example `INV_Spear_02` for a spear.
The generator refuses an icon that is no stock ID. It also refuses an icon that the core uses as a spell
marker (for example 108 for Flurry), unless the stock spell already has it. It reads the markers from `src/`.

## 4. Check

1. `cd forever/generator && uv run pytest`: the data pipeline test (the generated trees equal the spec,
   each spec spell is in the client and the server data, IDs, same bytes twice) and the refusal tests.
2. On a test server slot with the Forever rules (`wowai/test-realm/realm.sh --ticket <N> acquire --forever`),
   run the in-game checks of the Forever tickets, at least
   `wowai/test-realm/realm.sh --ticket <N> check wowai/test-realm/checks/t206_forever_talent.py forever`.
3. Install the new client data patch into the second client copy (`wowai/forever-client.sh`) and look at
   the talent window: positions, arrows, tooltips and icons.

## 5. Open the pull request

Open the refresh as a PR with the changed rows and header lines. Its text names the old and the new build,
the date, the rows that changed and why, and any disagreement between sources.
