# Record a dungeon route

A route file tells the bots where to walk in a dungeon and which packs to fight. You make it with GM commands. You never type coordinates.

**The way to make a route file is the trace and build flow:** you walk the dungeon one time as a group walks it, and the server records your path (the trace). Then `.route build` makes the route file around that path: the packs, the pull points, the quest objectives, and the boss list. Every route point lies on the path that you walked, so the bots never walk over a wall or lava that you did not walk. Sections 1 to 7 tell you how.

Two older ways stay for fixes and for tests (see "Other ways" at the end): `.route generate` (a route from the navmesh, whose path can cross walls and lava), and the hand recorder `.route add point` and `.route add pack`.

The route file of a dungeon is `bots/data/routes/<route>.txt` in the fork, for example `bots/data/routes/ragefire_chasm.txt`. Its trace is `bots/data/routes/<route>.trace.txt`, next to it. Commit both. The route format is in `src/bots/Route.h`, the trace format in `src/bots/RouteTrace.h`.

## 1. Start the test realm

Record on the test realm, never on the play realm. In WSL, in the fork:

```
WOWAI_DEV_SHARED_INSTALL=1 wowai/dev realm up
```

The shared test realm runs the build in the shared install folder `~/wowai/dev-install`. `WOWAI_DEV_SHARED_INSTALL=1` lets `wowai/dev` build into that folder. Without it, a build into the shared folder stops with an error.

`realm up` makes the folder `bots/data/routes/` of the fork and lets mangosd write into `bots/data/`. The recorder therefore writes the route file straight into your fork tree.

If the test realm already runs from another fork tree, run `WOWAI_DEV_SHARED_INSTALL=1 wowai/dev realm build` in this tree. The realm then uses this tree's build and this tree's `bots/data/`.

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

## 3. Install the RouteRecorder addon

The addon gives you buttons for the trace commands. It only sends the commands as chat lines, so you can also type them.

1. Close the client.
2. Copy the folder `wowai/addons/RouteRecorder` of the fork into `C:\Games\WoW-1.12.1\Interface\AddOns\`. You then have `Interface\AddOns\RouteRecorder\RouteRecorder.toc`. From WSL: `cp -r wowai/addons/RouteRecorder /mnt/c/Games/WoW-1.12.1/Interface/AddOns/`.
3. Start the client. On the character screen, click AddOns and make sure that RouteRecorder is on.
4. In the game, open Main Menu > Key Bindings, find "Route Recorder", and bind "Checkpoint" to a key, for example `F6`.

Type `/rr` to show or hide the frame. Drag the frame to move it. It shows the state of the trace (off, on, paused), the route name, the point count, the checkpoint count, and the length. It reads these from the answers of the server, for example `[route] trace on ragefire_chasm points=212 marks=9 length=640y`. A warning (yellow) or an error (red) shows under the counts. While a trace is on, the addon asks for the state line every 5 seconds and keeps those answers out of the chat frame.

`wowai/tests/route-recorder-addon.test.sh` checks the addon in Lua 5.0, the Lua of the 1.12 client, with a stub of the client API.

## 4. Walk the trace

Use `.gm on`, so that the creatures do not attack you. Go into the dungeon: for example `.tele RagefireChasm`, then walk into the portal. Stand at the entrance.

1. Click **Start** and type the route name, for example `ragefire_chasm`. The addon remembers the name for the next time.
2. Walk the run as a group does: in pull order, around the lava, to every spot that a quest needs (in Ragefire Chasm, the corpse of Maur Grimtotem), and to each boss. Walk on the ground: no fly and no teleport. Walk near each pack that the group must fight: the build takes a pack when your path comes within 25 yards of it (aggro range 20 plus 5).
3. At a corner, a shortcut, a ramp, or a narrow spot, press the **Checkpoint** key (or the button). A checkpoint becomes a route point at that exact spot, so the bots do not cut the corner.
4. For a break, or to walk back without recording, click **Pause**, and later **Resume**. Resume where you paused: a resume more than 4 yards away gets a warning, because the trace does not join the two spots.
5. After a wrong turn, click **Undo**. It removes the last 10 yards of the trace with their checkpoints. Walk back to the new end of the trace: no point is added until you are there.
6. At the last boss, click **Stop**. The server writes the trace file.

| Command | What it does |
|---|---|
| `.route trace start <route> [step=4] [floor=3] [jump=20]` | Starts a trace. The server adds your position each time that you move 4 yards from the last trace point, or 3 yards up or down (a floor change). If the trace file exists, the new walk goes at its end. |
| `.route trace mark [note]` | Adds a checkpoint where you stand. |
| `.route trace pause` | Stops adding points. The trace stays open. |
| `.route trace resume` | Adds points again, from where you stand. |
| `.route trace undo [yards]` | Removes the end of the trace, 10 yards by default. |
| `.route trace show` | Prints the state line: on or paused, the points, the checkpoints, and the length. |
| `.route trace stop` | Stops the trace and writes the file. |

A teleport, a flight, or a step of more than 20 yards does not go into the trace. The server prints a warning, and the next point starts a new part of the trace. The server writes the trace file after each command and after each 10 points.

**An extra walk.** After a build, the summary can name a quest pack that your trace misses (see section 5). Then walk an extra trace: stand on your first trace at the spot nearest the pack, `.route trace start ragefire_chasm` again, walk to the pack, and stop. The new walk goes at the end of the trace file. When the extra walk starts within 10 yards of the first trace, the route goes out along it and back at that spot. Run `.route build` again.

## 5. Build the route

```
.route build ragefire_chasm bosses=11517,11520,11518,11519
```

Without `bosses=`, the build takes the boss list of the existing route file. The four Ragefire Chasm bosses are Oggleflint (11517), Taragaman the Hungerer (11520), Jergosh the Invoker (11518), and Bazzalan (11519). The command works in the game, on the console, and over SOAP: it needs no character.

The build uses the rules of `.route generate`, with your trace as the center line:

- The packs: the creature groups and the spawns within 8 yards of each other.
- A pack is on the route when the trace comes within aggro range plus the margin of it. The route fights the packs in the order that the trace reaches them.
- A pull point on the trace before each pack, where the trace first comes that near, outside the aggro range of the pack.
- Route points along the trace every 10 yards, with a radius of 2 yards. Each checkpoint is a route point at its exact spot, and the 10 yards count again from it.
- The trace must pass each boss within aggro range.
- The quests of the dungeon: a visit point on the trace nearest the spot, and the kill counts and quest items that the packs on the trace meet.

The build does not add a pack off the trace, and it plans no path of its own. When the packs on the trace are too few, it prints the missing objective and the nearest packs that hold it, with their spawn and position. Walk an extra trace there (section 4), and build again.

It writes `bots/data/routes/<route>.txt` and prints a summary: the point count, the packs in order, the quests with their objectives, the boss list, each unmet boss or objective, and "Check:" lines for points to look at in the game. In the game, it also prints a walk check of the navmesh.

Settings, all optional, as `name=value`: `aggro` (20), `margin` (5), `cluster` (8), `spacing` (10), `radius` (2), `visit` (5: a visit spot must be this near the trace), `bosses`.

The build writes the route file again from the trace. A fix that you made with the hand recorder (`.route add mob`, `.route remove mob`) is lost: make it again after the build.

## 6. Walk the built route

| Command | What it does |
|---|---|
| `.route walk ragefire_chasm` | Loads the route file at its first step. |
| `.route next` | Goes to the next step. It shows the next point and the next pack, with the distance and state of each mob, and puts waypoint markers (wisps) on the places of the point for 2 minutes. |
| `.route back` | Goes to the step before, and puts wisps on its point in the same way. |
| `.route go` | Teleports you to the current step. |

The walk shows a trail of up to 100 wisps. A character may have only 100 summons, and a walk passes 100 points in less than 2 minutes. So when the trail is full, the oldest wisp moves to the new place, and its 2 minutes start again. A wisp that timed out leaves the trail, and the walk summons a new one. A new `.route walk` removes the wisps of your last walk. Each step answers with a line such as `Wisps: 0 new, 1 moved, 100 in the trail (at most 100); Thrallson has 100 of 100 summons.`

Check that the route follows your trace and has the right packs. When a pack is wrong, fix it with the recorder: `.route start ragefire_chasm`, then select a creature and use `.route add mob <pack>` or `.route remove mob`. Then `.route stop`.

## 7. Commit the files

The files are in your fork tree: `bots/data/routes/ragefire_chasm.txt` and `bots/data/routes/ragefire_chasm.trace.txt`. The answers in the game show the path inside the container, `bots/routes/...`, which is the same file. Check them and commit both.

## Other ways

### Record a route by hand

Go to the dungeon. Walk the route that the group must take, in order, and add each point and pack.

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

### Without a client

The commands also work on the mangosd console and over SOAP. There, `.route start` and `.route trace start` name an online character, for example a bot: `.route start ragefire_chasm Grukk`. The recorder then uses the position and the selection of that character. The console has one recording at a time.

### Generate a route

Stand in the dungeon (or name an online character in it), with flying off. Then:

```
.route generate ragefire_chasm [character] bosses=11517,11520,11518,11519 [seed=1]
```

The generator reads the spawns, creature groups, patrols, quests, and quest drops that the server loaded, and the navmesh of the map. It writes `bots/data/routes/<route>.txt` and prints the packs in order, the quests with their objectives, and a walk check. Without `bosses=`, it takes the boss list of the existing file. The same seed gives the same file, and the file records its seed.

Settings, all optional, as `name=value`: `seed`, `aggro` (20), `margin` (5), `pull` (35: the most path length from a pull point to its pack), `cluster` (8), `spacing` (10), `radius` (2), `side` (3), `alt` (7), `gap` (3), `wide` (10), `wall` (1.5). `src/bots/RouteGenerator.h` says what each one does.

A generated file also has a quest list (`quest`, `kill`, `item`, `visit` lines), and `quest <id>` at the end of the point and pack lines that are only there for a quest. A run without quests skips those.

### Export a map for the dungeon map

On the console (no character is needed):

```
.route export 389 [cluster=8] [bosses=11517,11520,11518,11519]
```

It writes `bots/data/routes/map_<id>.export.txt`: every pack of the map (its key is the spawn guid of its first mob, as in the route and the `.order.txt` file), each mob with its position, wander radius and patrol path, the quests with their objectives, the bosses, and the names. The dungeon map reads it without a running server. Without `bosses=`, the bosses come from the route files of the map. Keep `cluster` at the value of the route, or the pack keys differ. Run it again for each content patch, and commit the files. `src/bots/RouteExport.h` describes the format.
