# Game layer map

This page lists the facts of the game layer that agents look up again and again. It gives the core files, the database tables and columns, the GM commands that work over SOAP, and the paths of the logs and the game data.

Each fact was checked against the tree and a fresh check realm. The test `wowai/tests/game-layer-map.test.sh` checks them again:

```
wowai/tests/game-layer-map.test.sh               # the paths (no Docker)
wowai/test-realm/realm.sh --ticket <N> up        # a fresh check realm
wowai/tests/game-layer-map.test.sh --realm <N>   # also the columns and the GM commands
wowai/test-realm/realm.sh --ticket <N> down
```

If you change a path, a column, or a command row here, run the test. The test reads the tables below by their header rows, so keep the headers as they are.

## 1. Core folders and files

All paths are from the root of the fork. Guesses that were wrong in the version 1 run:

- `Groups/Group.h` is `src/game/Group/Group.h`.
- `Globals/ObjectMgr.h` is `src/game/ObjectMgr.h`.
- `Accounts/AccountMgr.cpp` is `src/game/AccountMgr.cpp`.
- `Server/Opcodes.cpp` is `src/game/Server/Protocol/Opcodes.cpp`.
- `MoveSpline.h` is `src/game/Movement/spline/MoveSpline.h`.

The fork's own code:

- `src/bots`: the bot decision logic. No game headers. GoogleTest tests in `src/bots/tests` (run them with `wowai/dev test`). See ADR 0002.
- `src/game/Bots`: the game layer of the bots.
  - `src/game/Bots/BotMgr.h`: `sBotMgr`. `src/game/Bots/BotAI.h`: the AI of one bot.
  - `src/game/Bots/BotHooks.cpp`: the hooks that upstream files call. `src/game/Bots/BotCommands.cpp`: the `.wowai` handlers.
  - Also `src/game/Bots/BotChat.cpp`, `src/game/Bots/BotGear.cpp`, `src/game/Bots/BotLoot.cpp`, `src/game/Bots/BotLevelRoutine.cpp`, `src/game/Bots/BotRun.cpp`, `src/game/Bots/RouteCommands.cpp`.
- `src/bots` and `src/game/Bots` are globbed with `CONFIGURE_DEPENDS`: a new file needs no CMake edit. The upstream lists in `src/game/CMakeLists.txt` are written by hand.
- `bots/data`: the bot data files. `bots/data/gear`, `bots/data/routes`, `bots/data/spells`, `bots/data/talents`, `bots/data/bottests`.
- `sql/migrations`: the database migrations (`wowai/dev realm migrate` applies the new ones).
- `wowai/test-realm/realm.sh`: the check kit. The Python client is `wowai/test-realm/check/wowclient.py` and `wowai/test-realm/check/kit.py`. An example check: `wowai/test-realm/checks/t42_death_rest.py`.
- Settings `Bots.*` and `BotChat.*`: `src/mangosd/mangosd.conf.dist.in`.

The upstream files that the bot code includes most (count of includes in `src/game/Bots`):

| File | What it holds |
|---|---|
| `src/game/Objects/Player.h` | `Player` (12 includes) |
| `src/shared/Log.h` | `sLog` (10) |
| `src/game/ObjectMgr.h` | `sObjectMgr`, item and creature templates (9) |
| `src/game/Server/WorldSession.h` | `WorldSession`, bot sessions (8) |
| `src/game/ObjectAccessor.h` | find a player or unit by guid or name (8) |
| `src/game/Maps/Map.h` | `Map` (7) |
| `src/game/Group/Group.h` | `Group` (7) |
| `src/game/Chat/Chat.h` | `ChatHandler`, the GM command declarations (7) |
| `src/game/SharedDefines.h` | enums: classes, races, spell results (5) |
| `src/game/ObjectGuid.h` | `ObjectGuid` (5) |
| `src/shared/Utilities/Random.h` | random numbers (4) |
| `src/game/Objects/Pet.h` | `Pet` (4) |
| `src/game/Movement/MotionMaster.h` | follow, point, and idle movement (4) |
| `src/game/Objects/ItemPrototype.h` | `ItemPrototype` (4) |
| `src/game/Objects/Bag.h` | `Bag` (4) |
| `src/game/Objects/Creature.h` | `Creature` (3) |
| `src/game/Maps/GridNotifiers.h` | the searchers for units near a point (3) |
| `src/game/Spells/SpellMgr.h` | `sSpellMgr` (2) |
| `src/game/World.h` | `sWorld` (2) |

Other files that agents look for:

- Units and spells: `src/game/Objects/Unit.h`, `src/game/Objects/Unit.cpp`, `src/game/Objects/Player.cpp`, `src/game/Spells/Spell.cpp`, `src/game/Spells/SpellAuras.h`, `src/game/Spells/SpellEntry.h`.
- Movement: `src/game/Movement/MotionMaster.cpp`, `src/game/Movement/TargetedMovementGenerator.h`, `src/game/Movement/spline/MoveSpline.h`.
- Packets: `src/game/Server/Protocol/Opcodes.h`, `src/game/Server/Protocol/Opcodes.cpp`, `src/game/Server/WorldPacket.h`, `src/game/Server/WorldSession.cpp`.
- Packet handlers: `src/game/Handlers/ChatHandler.cpp` (the chat hooks of the bots), `src/game/Handlers/GroupHandler.cpp`, `src/game/Handlers/CharacterHandler.cpp` (player login), `src/game/Handlers/LootHandler.cpp`, `src/game/Handlers/MovementHandler.cpp`.
- GM commands: the tables in `src/game/Chat/Chat.cpp` (a CRLF file), the handlers in `src/game/Commands` (for example `src/game/Commands/CharacterCommands.cpp`).
- World loop and startup: `src/game/World.cpp` (`sBotMgr` hooks), `src/mangosd/Master.cpp`.
- Group: `src/game/Group/Group.cpp`. Loot: `src/game/LootMgr.cpp`. Accounts: `src/game/AccountMgr.cpp`.
- Data stores: `src/game/Database/DBCStores.h`, `src/game/Database/SQLStorages.h`.
- The upstream login layer that the bots reuse: `src/game/PlayerBots/PlayerBotMgr.cpp`, `src/game/PlayerBots/PlayerBotAI.cpp`.
- The log: `src/shared/Log.cpp`.

## 2. Databases, tables, and columns

The realm has four game databases: `mangos`, `characters`, `realmd`, and `logs`. `mangos` is the world database. No database has the name `world`. `realmd` holds the accounts. The migration folder calls it `logon`. The chat service has its own database `botchat`. Run a query with `wowai/test-realm/realm.sh --ticket <N> sql <database> "<query>"`.

Names that were wrong in the version 1 run: `itemEntry` and `item` in `character_inventory` (the columns are `item_id` and `item_guid`), `RequiredLevel` and `InventoryType` in `item_template` (the columns are `required_level` and `inventory_type`). The world tables use snake_case, except `creature_loot_template`, `quest_template`, and `spell_template`, which use mixed case.

The columns that the checks use (not all columns of each table):

| Database | Table | Columns |
|---|---|---|
| `characters` | `characters` | `guid`, `account`, `name`, `race`, `class`, `level`, `xp`, `money`, `map`, `zone`, `position_x`, `position_y`, `position_z`, `online`, `health`, `power1`, `equipment_cache` |
| `characters` | `character_inventory` | `guid`, `bag`, `slot`, `item_guid`, `item_id` |
| `characters` | `item_instance` | `guid`, `item_id`, `owner_guid`, `count`, `durability`, `enchantments` |
| `characters` | `character_spell` | `guid`, `spell`, `active`, `disabled` |
| `characters` | `character_aura` | `guid`, `caster_guid`, `spell`, `duration` |
| `characters` | `character_pet` | `id`, `entry`, `owner_guid`, `level`, `name`, `slot` |
| `characters` | `groups` | `group_id`, `leader_guid`, `loot_method`, `loot_threshold`, `is_raid` |
| `characters` | `group_member` | `group_id`, `member_guid`, `assistant`, `subgroup` |
| `characters` | `bot_character` | `guid`, `account`, `role`, `world_bot` |
| `mangos` | `item_template` | `entry`, `name`, `class`, `subclass`, `quality`, `inventory_type`, `allowable_class`, `item_level`, `required_level`, `stat_type1`, `stat_value1`, `armor`, `dmg_min1`, `dmg_max1`, `delay`, `spellid_1` |
| `mangos` | `creature` | `guid`, `id`, `map`, `position_x`, `position_y`, `position_z`, `orientation`, `spawntimesecsmin`, `spawntimesecsmax` |
| `mangos` | `creature_template` | `entry`, `name`, `level_min`, `level_max`, `faction`, `rank`, `loot_id`, `gold_min`, `gold_max` |
| `mangos` | `creature_loot_template` | `entry`, `item`, `ChanceOrQuestChance`, `groupid`, `mincountOrRef`, `maxcount`, `condition_id`, `patch_min`, `patch_max` |
| `mangos` | `spell_template` | `entry`, `name`, `nameSubtext`, `spellLevel`, `manaCost`, `rangeIndex`, `effect1`, `effectApplyAuraName1` |
| `mangos` | `quest_template` | `entry`, `Title`, `MinLevel`, `QuestLevel`, `RewXP` |
| `mangos` | `game_tele` | `id`, `name`, `map`, `position_x`, `position_y`, `position_z`, `orientation` |
| `mangos` | `areatrigger_teleport` | `id`, `name`, `required_level`, `target_map`, `target_position_x`, `target_position_y`, `target_position_z` |
| `realmd` | `account` | `id`, `username`, `gmlevel`, `sessionkey`, `last_ip`, `last_login`, `online`, `os`, `platform` |
| `realmd` | `account_access` | `id`, `gmlevel`, `RealmID` |
| `realmd` | `realmlist` | `id`, `name`, `address`, `port` |

Notes:

- `character_inventory.bag` is 0 for the equipment and the backpack. Slots 0 to 18 are the equipment, 19 to 22 the bags, 23 to 38 the backpack.
- `bot_character` is the fork's table (migration in `sql/migrations`). `role` is `tank`, `healer`, `melee`, or `ranged`.
- The GM level of an account is in `account_access` (`gmlevel` 6 for the `.wowai` commands). mangosd reads it only at startup.
- A bot does not save its in-game changes unless `PlayerBot.AllowSaving = 1`. Run `.saveall` before you read a bot's rows.

## 3. GM commands over SOAP

Run a command with `wowai/test-realm/realm.sh --ticket <N> soap "<command>"`. SOAP runs a command like the server console: there is no player and no selection. If the row of a command in `src/game/Chat/Chat.cpp` allows the console (the third field is `true`), the command runs over SOAP. The other commands answer "This command is not available to you."

These commands work over SOAP. Each example ran on a fresh check realm, with its five bots online (Grukk, Hela, Shanka, Morzul, Ezzra):

| Command | Example | Answer has |
|---|---|---|
| `.server info` | `.server info` | `Players online` |
| `.character level <name> <level>` | `.character level Grukk 17` | `You changed level of Grukk to 17` |
| `.character level <name> <level>` | `.character level Grukk 16` | `You changed level of Grukk to 16` |
| `.revive <name>` | `.revive Grukk` | `Grukk revived` |
| `.repairitems <name>` | `.repairitems Grukk` | `You repair all Grukk's items` |
| `.saveall` | `.saveall` | `All players saved` |
| `.pet list <name>` | `.pet list Morzul` | `pets for character Morzul` |
| `.pet delete <pet id>` (the id from `.pet list`) | `.pet delete 999999` | `Pet #999999 not found` |
| `.tele name <name> <place>` | `.tele name Hela Orgrimmar` | `You are teleporting Hela to Orgrimmar` |
| `.send items <name> "<subject>" "<text>" <item>:<count>` | `.send items Grukk "map" "map" 6948:1` | `Mail sent to Grukk` |
| `.send money <name> "<subject>" "<text>" <copper>` | `.send money Grukk "map" "map" 100` | `Mail sent to Grukk` |
| `.character reputation <name>` | `.character reputation Grukk` | `Booty Bay` |
| `.lookup tele <part of name>` | `.lookup tele ragefire` | `RagefireChasm` |
| `.lookup spell <part of name>` | `.lookup spell Frostbolt` | `Frostbolt, rank 1` |
| `.lookup item <part of name>` | `.lookup item Hearthstone` | `6948 - Hearthstone` |
| `.lookup creature <part of name>` | `.lookup creature Taragaman` | `11520 - Taragaman the Hungerer` |
| `.lookup area <part of name>` | `.lookup area Ragefire` | `Ragefire Chasm` |
| `.list creature <entry>` | `.list creature 11520` | `MapId:389` |
| `.list item <entry>` | `.list item 6948` | `owner: Grukk` |
| `.instance stats` | `.instance stats` | `instances loaded` |
| `.account onlinelist` | `.account onlinelist` | `Character` |
| `.reload config` | `.reload config` | `World config settings reloaded` |
| `.wowai bot logout <name>` | `.wowai bot logout Shanka` | `Bot Shanka logs out` |
| `.wowai bot login <name>` | `.wowai bot login Shanka` | `Bot Shanka logs in` |
| `.wowai bot create <name> <class> <role> [race]` | `.wowai bot create Grukk warrior tank` | `A character named Grukk exists already` |
| `.wowai gear equip <name>` | `.wowai gear equip Grukk` | `Grukk wears warrior_tank` |
| `.wowai run status` | `.wowai run status` | `No run` |
| `.wowai run stop` | `.wowai run stop` | `No run is running` |
| `.route show` | `.route show` | `No route is being recorded` |

More notes:

- `.wowai bot create` makes a level 16 bot and logs it in. A new name answers "Bot <name> made. It logs in now." `.wowai run start <script> <scenario>` starts a scripted run.
- `.kick <name>` works over SOAP, but do not kick a bot: the bot entry stays online and `.wowai bot login` then answers "online already".
- `.character level` calls `GiveLevel` on an online bot: a good level-up test.
- An empty answer prints an empty line. A wrong command answers "There is no such command".

These commands need a player in the game. Over SOAP they answer "This command is not available to you." Run them with the client of the check kit, as the GM player, with the target selected. For example: `client.gm(".die", select=guid)`. See `wowai/test-realm/check/wowclient.py` and `wowai/test-realm/checks/t42_death_rest.py`. The GM account needs GM level 6.

| Command | Example | Answer has |
|---|---|---|
| `.die` | `.die Grukk` | `not available` |
| `.replenish` | `.replenish Grukk` | `not available` |
| `.unaura all` | `.unaura all` | `not available` |
| `.cooldown clear` | `.cooldown clear` | `not available` |
| `.namego <name>` | `.namego Grukk` | `not available` |
| `.goname <name>` | `.goname Grukk` | `not available` |
| `.pinfo <name>` | `.pinfo Grukk` | `not available` |
| `.combatstop <name>` | `.combatstop Grukk` | `not available` |
| `.instance unbind all` | `.instance unbind all` | `not available` |
| `.modify hp <current> <max>` | `.modify hp 100` | `not available` |
| `.damage <amount>` | `.damage 10` | `not available` |
| `.aura <spell>` | `.aura 5782` | `not available` |
| `.list auras` | `.list auras` | `not available` |
| `.list threat` | `.list threat` | `not available` |
| `.unit statinfo` | `.unit statinfo` | `not available` |
| `.gps` | `.gps` | `not available` |
| `.tele <place>` | `.tele RagefireChasm` | `not available` |
| `.freeze` | `.freeze Grukk` | `not available` |
| `.respawn` | `.respawn` | `not available` |
| `.npc spawn add <entry>` | `.npc spawn add 3098` | `not available` |
| `.learn all_trainer` | `.learn all_trainer` | `not available` |
| `.gm off` | `.gm off` | `not available` |

Notes on these: `.die` with `GM.CreditOnDie = 1` makes the GM the killer. `.revive` leaves half health and mana. `.npc spawn add` needs GM level 5 or more. `.npc add` is the vendor command. Put `.gm off` before a `.die` on the GM's own player.

## 4. Logs and data paths

- The realm folder: `~/wowai/wt/t<N>-realm` for a check realm, `~/wowai/test-realm` for the shared test realm. Its `.env` holds the settings, the ports, and the GM account (`WOWAI_REALM_GM_ACCOUNT`, `WOWAI_REALM_GM_PASSWORD`).
- The server log: `<realm folder>/storage/mangosd/logs/Server.log`. Each mangosd start empties it. The same folder has `DBErrors.log`, `Chat.log`, `LevelUp.log`, `Loot.log`, and more. The realmd log: `<realm folder>/storage/realmd/logs/Realmd.log`. Use `realm.sh --ticket <N> mark` and `realm.sh --ticket <N> log` to read only the new lines. `down` keeps the logs in `~/wowai/wt/t<N>-logs/<time>/`.
- The kit sets `LogLevel.File = 3`, so the `[Bots]` DETAIL lines are in Server.log.
- The game data (DBC files, maps, vmaps, mmaps): the folder that `WOWAI_REALM_DATA` names in the realm `.env`. Print it with `realm.sh --ticket <N> env`. Today it is `/mnt/c/Games/vmangos-deploy/storage/mangosd/extracted-data`. Inside the mangosd container it is `/opt/vmangos/storage/data`. Do not search for it with `find /`: that scans `/mnt/c` and times out.
- The bot data files: the folder that `WOWAI_BOTS_DATA` names (default: `bots/data` of the fork tree that ran `realm up`).
- The mangosd configuration: `<realm folder>/config/mangosd.conf`. The template is `src/mangosd/mangosd.conf.dist.in`.
