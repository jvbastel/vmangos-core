# Refresh of the Forever spec

The spec is the folder `forever/spec`, one file per area. The generator in `forever/generator` reads every
`*.tsv` file in it and makes all Forever data from them. Nobody edits the generated files by hand.

- `warrior.tsv` holds the Forever rules of the Warrior (spec jvbastel/WOWAI#204): one row per talent, per
  new ability and per changed base ability.
- `blacksmithing.tsv` holds the Blacksmithing recipes (spec #225): one row per new recipe spell (kind
  `recipe`), one row per stock recipe that Forever changes in place (kind `stock`, #228), and one row per
  stock recipe that Forever took away (kind `removed`, #229).
- `blacksmithing_trainers.tsv` says how a character learns the new recipes that the Forever trainers teach
  (#239): one row per recipe, kind `trainer` (the trainers teach it) or `auto` (it comes with the profession
  skill). It is a file of its own, not a column of `blacksmithing.tsv`, because `tools/new_recipe_rows.py`
  writes the #230 rows there and would drop a hand-filled column on a rerun.
- `items.tsv` holds the new and changed items (spec #225): one row per item.
- `item_sets.tsv` holds the new and changed item sets (#231): a `set` row (a new set) or a `stockset` row (a
  changed stock set), each with ID, name and pieces, and one `bonus` row per set bonus.
- `mining.tsv` holds the Mining rows (spec #225, #232): the new ores (kind `ore`, new items like those of
  `items.tsv`), one row per ore and vein loot ID (kind `drop`), and the Forever mining content that is not
  ported (kind `cut`, listed in the cut list).
- `encrypted.tsv` lists the profession items that the Forever client holds in encrypted data (US42 of #225).
  The spec has no row from them, and the generator writes them into `encrypted-list.tsv` next to the cut
  list. At a refresh, try them again: a row that the client can now read leaves this file for its own file.

- `loot_<dungeon>.tsv` and `loot_<dungeon>_items.tsv` hold the Forever loot of one dungeon (spec #240): the boss
  drops and quest rewards, and the items. Section "Dungeon loot" below has the layout.

Each header names the sources, the Forever build, the date, its free ID range, the next free IDs and each
column. Within a file, the rows of one ticket stay in a block under a comment line that names the ticket.

## Free ID ranges

| IDs | Range | Named in | Notes |
|---|---|---|---|
| Spells, talents, skill line abilities | 60000-65535 | `warrior.tsv` | spell IDs are SMALLINT UNSIGNED in the world database |
| Profession spells (recipes) | 61000-61999 | `blacksmithing.tsv` | a block of the spell range; the core can tell a profession spell from a Warrior spell by its ID, even with the rules off. The generator refuses a recipe outside it |
| Items | 70000-79999 | `items.tsv` | the stock 1.12 world database ends at item 25818. A changed stock item keeps its own ID |
| Dungeon loot items | 71000-71699 | each `loot_*_items.tsv` (`# Item ID block:`) | a block of the item range, one block of 100 per ticket (below) |
| Dungeon loot references | 31000-31699 | REFRESH.md | new `reference_loot_template` IDs, above every stock reference (the stock world database ends at 30576); one block of 100 per ticket (below) |

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
- A stock row (`kind` stock, `spell` = `sourceSpell` = the stock spell) holds the Forever colour range, the
  skill that its trainer rows and its plans need (`minRank`, `plans`), and the Forever reagents, name and
  focus. Compare `recipes.tsv` of the client dump with the stock world DB (skill_line_ability, npc_trainer,
  item_template). The plans skill comes from the Forever plans item where the client can read it; the
  trainer skill is not in the client and moves with the colour range.
- An item (`items.tsv`) is a copy of the stock row of `sourceItem` (class, subclass, slot, material,
  durability). A new item (kind `item`) takes a free item ID; pick a stock source item of the same kind.
  A changed stock item (kind `stock`) is its own source and keeps its stock ID. Base stats go into `stats`,
  resistances into `resistances`; every other stat is a bonus in `bonuses` ("SpellPower 5"), which the
  generator turns into a stock equip spell (the table in `generator/forever_gen/equip_stats.py`). A Forever
  value with no stock spell is rounded to the nearest one, up on a tie, and keeps its Forever value:
  "SpellPower 11 (Forever 10)". The generator refuses any other value. Keep the Forever stats in
  `foreverStats`. `displayId` is a stock `ItemDisplayInfo.dbc` ID, which also gives the icon; the generator
  refuses any other. The changed stock items of #227 come from the dumps by a script outside git
  (`notes/forever/client-data/tools/t227/build_rows.py`).

- A removal row (`kind` removed, `spell` = `sourceSpell` = the stock spell) takes a stock recipe away on the
  Forever realm: a stock recipe of `recipes.tsv` (group `stock-missing-in-fv`) that the Forever client does not
  have. Fill `kind`, `spell`, `sourceSpell`, `name`, `skillLine`, `plans` and `notes`; write `-` in the
  other columns. The recipe leaves the client skill line and every trainer; its spell stays, so that a
  character who knows it keeps it.

- New recipes and items (#230): `generator/tools/new_recipe_rows.py` writes the `# --- #230` blocks of
  `blacksmithing.tsv` and `items.tsv` from the client dump (`fv_new_recipes.tsv`, `items.tsv`) and the stock
  world DB. It is a row writer, not a dump tool: it reads the dump outside git and keeps the IDs that a
  block already gave out on a rerun. With `--stock-item-ids` it also writes
  `generator/forever_gen/stock_item_ids.txt` (the stock `item_template` IDs), which the refusals read. Never
  edit the #230 blocks or that file by hand.
- A new recipe with plans names them as `<item>/<learn spell>`: the plans item is an `items.tsv` row, and the
  learn spell (a copy of stock spell 2760) takes the next free ID of the profession spell range. The plans
  need the recipe's `minRank`.
- Trainers of new recipes (`blacksmithing_trainers.tsv`, #239): the client has no trainer data, so the source is
  a fan source that marks a recipe as a trainer recipe (the header names them). A `trainer` row names the
  recipe, the learn spell of its plans (the trainer casts it; the plans stay), and `trainersFrom`: a stock
  recipe that the stock trainers teach at about the recipe's skill, never below it. The generator copies the
  stock npc_trainer rows of that recipe's learn spell into forever_npc_trainer with the new learn spell, the
  recipe's `minRank` and `cost`. So every trainer that teaches the stock recipe teaches the new one, and a
  trainer whose rank caps below the skill does not. The generated SQL fails when no trainer teaches the stock
  recipe, when one teaches it below the skill, or when a trainer template teaches it. No Forever source gives
  the cost: take the stock `spellcost` of `trainersFrom` and say in the notes that it is a guess. An `auto`
  row (a recipe that comes with Apprentice, as stock Rough Copper Vest 12260) sets AcquireMethod 1 on the
  recipe's skill line ability, in the client and the server. The #230 note "trainer: a later ticket" of a
  recipe is the row writer's text: the trainer file is what holds.
- The generator refuses a new recipe or item without its Forever source (`foreverSpell`, `foreverItem`), a
  reagent or made item that is neither a stock item nor an `items.tsv` row, and a new item whose
  `sourceItem` is no stock item.
- The dead-content rule (`generator/forever_gen/dead_content.py`) keeps every readable new row in the spec
  but leaves out of the generated data each new recipe that needs a Forever-only station (Master Forge,
  Molten Foundry), each recipe and item that only cut rows use or make, and the plans of a cut recipe. The
  generator prints the cut list and writes it to `cut-list.tsv`. Review it at each refresh.
- Item sets (`item_sets.tsv`, #231): the Forever client tables ItemSet and ItemSetSpell give the pieces and
  the bonus spells (`SpellEffect` gives each spell's aura and value). Write each bonus as the stock equip spell
  of the same aura and value, as in `items.tsv`, plus `Strength`, `ShadowRes` and `HealthRegen`
  (`generator/forever_gen/item_sets.py`). A bonus with no 1.12 aura gets `none` and a note. A set keeps its
  Forever ID. A piece is an `items.tsv` row (the generator puts the set ID on its row) or a stock piece of
  the same stock set. The generator writes ItemSet.dbc into the Forever DBC folder (the core reads it there)
  and the client data patch.
- Ores and veins (`mining.tsv`): the client has no drop rates and no spawns. A drop row names the loot ID of
  its veins (`gameobject_template.data1`, read on a test realm slot: `realm.sh --ticket N sql mangos ...`) and
  the stock gameobjects that use it; the generated SQL fails if one does not. The generator copies every stock
  row of the loot ID into `forever_gameobject_loot_template` and adds the ore (the core reads the table by
  loot ID). `rate` says where the chance comes from: `spec` (the spec's choice) or `fallback` (the stock gem
  rate of the vein, until launch data gives a rate). When launch data gives a rate, change `chance` and set
  `rate` to `spec`. A drop of an ore that the dead-content rule cuts is left out and listed in the cut list.

### The removal marker

Every Forever override table (`forever_<stock table>`) has one more column than its stock table:
`forever_removed`. A row with `forever_removed` = 1 is a copy of the stock rows of one key, and takes them
away with the rules on: the core reads neither the stock rows of that key nor the marked row
(`Forever::Table` and `SelectWithForeverRows` in `src/game/Forever/Forever.cpp`). With the rules off the core
reads no Forever table, so the stock rows load as usual. The generator adds the column at the end of
`forever_world.sql`, after every other row, because a copy with `SELECT *` has one column too few once the
column is there. A new kind of removal (items, loot, item sets) adds its override table to
`build.FOREVER_TABLES` and writes `removals.remove_rows(...)` lines into the `removed` list at the end of
`build.server_sql`; the module docstring of `generator/forever_gen/removals.py` says how. Spells cannot be
removed: the core would delete them from every character who knows them.

## Dungeon loot (spec #240)

The Forever loot of the six dungeons of the Forever beta. Each dungeon has two files in `forever/spec`, named
`loot_<slug>.tsv` (drops and quest rewards) and `loot_<slug>_items.tsv` (items). Two files, not one, because the
item rows and the drop rows have different columns; the generator tells them apart by their column line
(`generator/forever_gen/loot.py`, `ITEM_COLUMNS` and `LOOT_COLUMNS`). The header of each file explains every column.

| Dungeon | Slug | Ticket | Item ID block | Reference ID block |
|---|---|---|---|---|
| Ragefire Chasm | `ragefire_chasm` | #242 | 71000-71099 | 31000-31099 |
| Wailing Caverns | `wailing_caverns` | #243 | 71100-71199 | 31100-31199 |
| The Deadmines | `deadmines` | #244 | 71200-71299 | 31200-31299 |
| Shadowfang Keep | `shadowfang_keep` | #245 | 71300-71399 | 31300-31399 |
| Blackfathom Deeps | `blackfathom_deeps` | #246 | 71400-71499 | 31400-31499 |
| The Stockade | `stockade` | #247 | 71500-71599 | 31500-31599 |
| Item sets (`loot_item_sets.tsv`) | - | #248 | 71600-71699 | 31600-31699 |

An items file names its block in the header (`# Item ID block: 71000-71099.`) and its next free ID. The generator
refuses a new item outside the block of its file and two files whose blocks overlap. No ticket uses a reference
block yet: the generator does not write new reference entries (the ticket that needs one adds that in its own
function of `loot.py`).

Row kinds:
- Items file. `stock`: a stock boss drop or quest reward with its Forever values (it keeps its ID). `item`: a new
  item with a stock twin (`sourceItem`). `new`: a new item with no stock twin: `class`, `subclass` and
  `inventoryType` come from the client table `Item.db2`, which the Forever client can read even where ItemSparse
  is encrypted. The generator copies the stock weapon or armor item of that class, subclass and slot nearest in
  item level (then of the same quality, then the lowest ID) from `generator/forever_gen/stock_world/gear.tsv`, and
  takes its display; a `displayId` overrides it (a stock `ItemDisplayInfo.dbc` ID). Stats and bonuses work as in
  `items.tsv`. A Forever stat or effect with no 1.12 meaning or no stock equip spell (Haste, Expertise, a proc
  that 1.12 has no spell for, "+6 Attack Power against Beasts") goes into `leftOut` and stays off the item. Each
  new item, and each stock item that the gear file has, takes the disenchant ID of the stock gear of its class
  and quality nearest in item level.
- Loot file. `drop`: a new item on a boss (creature entry and stock name), with its Forever chance; `loot` is
  `creature` (the boss's creature loot) or `reference <ID>` (a reference that only this boss uses). The
  generator copies every stock row of that loot entry into `forever_creature_loot_template` (or
  `forever_reference_loot_template`) and adds the new row with group 0 (outside every stock equal-chance group),
  patch_min 0 and patch_max 10: the six dungeons are open from the first content patch. A stock boss drop keeps
  its stock row, chance and group: it needs only its items row. `reward`: a new reward choice of a quest
  (`replaces` "-": the first empty choice slot) or a swapped one (`replaces` the stock choice). The generator
  copies every stock row of the quest into `forever_quest_template` with the new choices. `unlock`: a stock reward
  choice that loses its class lock (the item needs an items row; the generator sets its `allowable_class` to -1).
  A reward row with `replaces` `fixed` (#244, quest 167): the item is a stock fixed reward that Forever offers as a
  choice; it goes into the first empty choice slot and out of the fixed reward slots (the SQL fails when a stock row
  of the quest does not have it as a fixed reward). A `stock` items row of an item that is not in the stock gear
  file (a white stock item that Forever makes blue) can name its `class`: it gives the disenchant ID.
- Every row has `status`, `source` and `date`. `status`: `confirmed` (the source shows it), `unconfirmed` or
  `unreadable` (the source cannot be read by script). A row that is not confirmed is left out, and so are the drops
  and rewards of its item. `source`: a Wowhead Forever URL (the tooltip service
  `https://nether.wowhead.com/tooltip/item/<id>?dataEnv=16&locale=0` or a `https://www.wowhead.com/forever/...`
  page) or `client <build>`. `date`: the day the source was read (YYYY-MM-DD).

The confirmed rule (spec #240): a new boss drop is confirmed when the Wowhead tooltip's "Dropped by" names the boss;
its chance is the tooltip's "Drop Chance". A quest reward change is confirmed when the Wowhead Forever quest page
shows it (read it with a web-fetch tool: `www.wowhead.com` answers 403 to scripts; never work around that). A lifted
class lock is confirmed when the tooltip shows it. Item stats come from the client where it can read them, else from
the tooltip. `generator/tools/wowhead_tooltips.py <item ID> ...` reads the tooltips with a pause between requests,
keeps each raw answer in `notes/forever/wowhead-cache/item/` (outside git) and prints the fields the rows need.

The generator writes `loot-list.tsv` next to the cut list: each `leftOut` value (list `left-out`) and each row that
is not confirmed (list `unconfirmed` or `unreadable`). Review it at each refresh.

The generator has no world database. It reads the stock creatures (entry, loot ID, name), quests (title, reward
choices), references (the creature loot entries that use each) and gear from `generator/forever_gen/stock_world/`,
which `generator/tools/stock_world_rows.py` writes from the shared test realm (SELECT only). Run it again after an
upstream merge. The generated SQL also fails at load when a boss does not use its loot entry, a reference has other
users, or a quest's stock choices are not those of the stock data.

It refuses a row with no source or date, an item ID clash (an entry twice, outside its block, a block overlap), a
non-stock display or equip spell, an unknown boss (or a boss under another name), an unknown quest (or another
title), an unknown item, a reference that other creatures use, a replaced choice that the quest does not have, and
a quest with no free choice slot.

The core reads `forever_creature_loot_template`, `forever_reference_loot_template` and `forever_quest_template` only
with the rules on, over the stock tables by `entry` (`Forever::Table` in `LootStore::LoadLootTable`,
`Forever::SelectWithForeverRows` in `ObjectMgr::LoadQuests`). With the rules on it stops at startup when one of them
is missing (migration `sql/migrations/20261031120000_world.sql` makes them).

At the refresh after the Forever launch: extract the client again; for each `source` that is a Wowhead URL whose item
the client can now read, put the client values in, set `source` to `client <build>`, and list every value that
changed in the PR.

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
   For the dungeon loot, the three steps of `wowai/test-realm/checks/t242_rfc_loot.py` (its docstring).
3. Install the new client data patch into the second client copy (`wowai/forever-client.sh`) and look at
   the talent window: positions, arrows, tooltips and icons.

## 5. Open the pull request

Open the refresh as a PR with the changed rows and header lines. Its text names the old and the new build,
the date, the rows that changed and why, and any disagreement between sources.
