# Record a dungeon route

A route file tells the bots where to walk in a dungeon and which packs to fight. You make it with GM commands. You never type coordinates.

There are two ways:

- Generate it with `.route generate` from the world database and the navmesh (see "Generate a route" below). Then walk it once in the game with `.route walk` and fix wrong packs with the recorder. This is how `ragefire_chasm.txt` was made.
- Record it by hand while you walk the dungeon (sections 1 to 4).

The route file of a dungeon is `bots/data/routes/<route>.txt` in the fork, for example `bots/data/routes/ragefire_chasm.txt`. The format is in `src/bots/Route.h`.

## 1. Start the test realm

Record on the test realm, never on the play realm. In WSL, in the fork:

```
wowai/dev realm up
```

`realm up` makes the folder `bots/data/routes/` of the fork and lets mangosd write into `bots/data/`. The recorder therefore writes the route file straight into your fork tree.

If the test realm already runs from another fork tree, run `wowai/dev realm build` in this tree. The realm then uses this tree's build and this tree's `bots/data/`.

The GM account and its password are in `~/wowai/test-realm/.env` (`WOWAI_REALM_GM_ACCOUNT` and `WOWAI_REALM_GM_PASSWORD`).

## 2. Point the client at the test realm

The test realm uses login port 3725. The play realm uses the normal port 3724.

1. Close the client.
2. Open `C:\Games\WoW-1.12.1\realmlist.wtf` in a text editor.
3. Change the line to:

   ```
   set realmlist 127.0.0.1:3725
   ```

4. Start the client and log in with the GM account. If the account has no character, make one. Any class works.

When you finish, set the line back to `set realmlist 127.0.0.1` for the play realm.

## 3. Record the route

Go to the dungeon. For example, `.tele RagefireChasm` takes you to the entrance in Orgrimmar. Walk into the portal. Then walk the route that the group must take, in order.

| Command | What it does |
|---|---|
| `.route start ragefire_chasm` | Starts the recording. If the file exists, the recorder reads it and adds to its end. |
| `.route add point [name] [radius]` | Adds a point where you stand. The radius is in yards, 2 by default. Without a name, the points are `p1`, `p2`, and so on. |
| `.route add alt [radius]` | Adds one more place for the last point. A bot picks one place of the point. |
| `.route add pack [name]` | Select one creature of a pack first. Adds the creature and the other creatures of its creature group. Without a selection, it takes the nearest hostile creature within 20 yards. Without a name, the packs are `pack1`, `pack2`, and so on. |
| `.route add mob [pack]` | Adds the selected creature to the last pack, or to the pack of that name. Use it for a pack whose creatures are not in one creature group. |
| `.route remove mob` | Takes the selected creature out of its pack. |
| `.route add boss` | Adds the entry of the selected creature to the boss list. |
| `.route undo` | Removes the last point or pack. |
| `.route show` | Shows the route so far. |
| `.route stop` | Stops the recording. |

Each command writes the file at once. The answer of `.route add pack` names each creature that it added, so you can check the pack.

Tips:

- Use `.gm on` so that the creatures do not attack you.
- Add a point before each pack, at the spot where the group waits for the pull.
- For a boss, select the boss, then use `.route add pack <name>` and `.route add boss`.
- The four Ragefire Chasm bosses are Oggleflint (11517), Taragaman the Hungerer (11520), Jergosh the Invoker (11518), and Bazzalan (11519).
- When the recorder writes the file again, comments that you added by hand are lost.

## 4. Commit the file

The file is in your fork tree, for example `bots/data/routes/ragefire_chasm.txt`. The answers in the game show the path inside the container, `bots/routes/ragefire_chasm.txt`, which is the same file. Check it and commit it.

## Without a client

The commands also work on the mangosd console and over SOAP. There, `.route start` names an online character, for example a bot: `.route start ragefire_chasm Grukk`. The recorder then uses the position and the selection of that character. The console has one recording at a time.

## Generate a route

Stand in the dungeon (or name an online character in it), with flying off. Then:

```
.route generate ragefire_chasm [character] bosses=11517,11520,11518,11519 [seed=1]
```

The generator reads the spawns, creature groups, patrols, quests, and quest drops that the server loaded, and the navmesh of the map. It writes `bots/data/routes/<route>.txt` and prints the packs in order, the quests with their objectives, and a walk check. Without `bosses=`, it takes the boss list of the existing file. The same seed gives the same file, and the file records its seed.

Settings, all optional, as `name=value`: `seed`, `aggro` (20), `margin` (5), `cluster` (8), `spacing` (10), `radius` (2), `side` (3), `alt` (7), `gap` (3), `wide` (10), `wall` (1.5). `src/bots/RouteGenerator.h` says what each one does.

A generated file also has a quest list (`quest`, `kill`, `item`, `visit` lines), and `quest <id>` at the end of the point and pack lines that are only there for a quest. A run without quests skips those.

## Walk a route in the game

| Command | What it does |
|---|---|
| `.route walk ragefire_chasm` | Loads the route file at its first step. |
| `.route next` | Goes to the next step. It shows the next point and the next pack, with the distance and state of each mob, and puts waypoint markers on the places of the point for 2 minutes. |
| `.route back` | Goes to the step before. |
| `.route go` | Teleports you to the current step. |

Use `.gm on`, so the packs do not attack you. When a pack is wrong, fix it with the recorder: `.route start ragefire_chasm`, then select a creature and use `.route add mob <pack>` or `.route remove mob`. Then `.route stop`. The recorder keeps the quests and the seed of the file.
