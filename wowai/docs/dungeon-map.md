# Make a route on the dungeon map

The dungeon map is a local web page. It shows the navmesh floor of a dungeon from above, with every pack, patrol, wanderer, boss and quest objective, and the route on it. On the page you draw the route line, set the pack order, skip packs and add packs. Save writes two files beside the route file. Then `.route build` on a test realm slot makes the route file from them. No one walks the dungeon in the game (ADR 0003 in the WOWAI repo).

The flow is **map → Save → `.route build` on a slot → full run → walked layer**.

1. Open the map of the route, and draw or edit the line and the pack order (sections 2 to 5).
2. Save the trace and the order (section 6).
3. Build the route file with `.route build` on a test realm slot (section 7).
4. The player starts a scripted full run of the new route.
5. Load the walked path of that run on the map, and judge the route (section 8).

The tool is `bots/tools/route-map/` in the fork. The route files are in `bots/data/routes/`: the route `<route>.txt`, the trace `<route>.trace.txt`, the hand pack order `<route>.order.txt`, and the export of the map `map_<id>.export.txt`. The file formats are in `src/bots/Route.h`, `src/bots/RouteTrace.h`, `src/bots/RouteGenerator.h` (ParsePackOrder) and `src/bots/RouteExport.h`. For the trace commands in the game, and for the rules of the order file, see `wowai/docs/route-recorder.md`.

**Use dungeon maps only.** The tool reads every navmesh tile of a map. The tiles of a continent fill the memory of the WSL machine. The continents are map 0 (Eastern Kingdoms) and map 1 (Kalimdor). The tool refuses a route on a continent map with an error.

## 1. Start the map

The tool needs `uv`. In WSL, in the fork:

```
bots/tools/route-map/route-map ragefire_chasm
```

The argument is the route name, the name of a route file in `bots/data/routes/` without `.txt`. The map id comes from the route file. For a dungeon that has no route file yet, make a first draft with `.route generate` (see route-recorder.md, "Generate a route").

The page is `http://127.0.0.1:8766/?route=ragefire_chasm`. Under WSL it opens in the Windows browser. Ctrl+C stops the tool.

Options:

| Option | What it does |
|---|---|
| `--navmesh DIR` | The navmesh folder: the `mmaps` folder of the extracted data of the server. |
| `--settings FILE` | Another settings file. The default is `bots/tools/route-map/settings.toml`. |
| `--port N` | The port of the page, 8766 by default. |
| `--no-browser` | Do not open a browser. |

`settings.toml` gives the folders: `navmesh` (the `mmaps` folder, which `--navmesh` overrides), `routes` and `exports` (both `bots/data/routes`). A relative path is relative to the folder of the settings file. If your extracted data is in another place, set `navmesh` there, or use `--navmesh`.

The packs, patrols, bosses and quests come from the export file of the map, `map_<id>.export.txt`. Without it the page shows only the packs of the route. To make it, run `.route export <map id>` on the console (route-recorder.md, "Export a map for the dungeon map"), and commit the file.

## 2. Read the map

- **Heights** and **Show**: draw only the floor between two heights, for a dungeon with floors one above the other. **Fit** shows the whole map. Drag to move the map, and use the mouse wheel to zoom.
- The route line is red. A blue cross is the pull point of a pack.
- A filled dot is a pack on the route, with its place in the pack order and its `packN` name. A hollow, dim dot is a pack off the route. A boss pack shows the boss name.
- A patrol path has arrows in the direction of the walk. A circle round a mob is how far it wanders.
- A quest badge on a pack shows that its mobs count for a quest objective (hover for the count). A pin is a quest spot to visit. The list of objectives that the route does not meet names the packs that can meet them. Click one to show it. An objective counts the packs that the built route pulls: the packs on the route and the added packs, but not the skipped packs.

## 3. Set the pack order

The side list shows the pack order of the route in two parts. The hand order is the packs of the order file, with the numbers `h1`, `h2`, and so on. The order of the generator is the other packs of the route, in route order.

- **Order mode**: click packs one after another. Each click gives the pack the next number of the hand order.
- Drag a pack in the list to a new place, or from the part of the generator into the hand order.
- The x button takes a pack out of the hand order. It goes back to the order of the generator.

The packs that the hand order does not name keep the order of the generator, or of the trace for `.route build`.

## 4. Skip and add packs

- **Skip mode**: click a pack to skip it. The route does not pull it. Click it again to take the skip off.
- A skipped pack shows its aggro circle. If the route line comes within the circle, the circle is red: the pack can join a fight as an extra pack. The warning under the list names the pack, the distance and the route point.
- **Add a pack**: in order mode, click a pack that is off the route. It gets the next number, and the route goes to pull it there, with a path to its pull point and back to the line.

## 5. Draw the route line

- **Load route line**: start from the line of the route file (for example a generated route) as a draft.
- **Load trace**: edit the trace that you drew or walked before, with its marks and walks.
- **Draw**: each click adds a point at the end of the line, on the floor. Where floors stack, choose the floor of the point. The page joins two points with a path along the navmesh floor. A piece with a warning (over lava, or a long way round) has another colour.
- Drag a point to move it. Right-click a point to delete it. **Clear line** removes the whole line.
- **Mark**: select a point (click it), then Mark makes it a quest visit mark.
- **Branch**: select a point of the main line, then Branch starts a branch there. The next clicks draw the branch, for example to a side room. **End branch** ends it. The build walks a branch out and back.

## 6. Save

- **Save trace** writes `<route>.trace.txt`: the floor path of each piece of the line, as points a few yards apart.
- **Save order** writes `<route>.order.txt`: one `pack` line per pack of the hand order, and one `skip` line per skipped pack. The comments that you wrote by hand in the file stay. The rules of the file are in route-recorder.md, "Hand pack order (`<route>.order.txt`)".

Save never writes the route file. The page asks before you leave it with an unsaved order.

## 7. Build the route on a slot

Build on a test realm slot, never on the play realm. Copy the saved files into the slot's routes folder, or build the slot from your fork tree. Then, on the console or over SOAP:

```
.route build ragefire_chasm <character> bosses=11517,11520,11518,11519
```

The character must stand on the map of the dungeon, because a `pack` line of the order file needs the navmesh of the map. Without `pack` lines no character is needed. Read the summary and its `Check:` lines, then commit the route file, the trace and the order file. Section 5 of route-recorder.md tells you more about the build.

## 8. Load the walked path

A scripted full run writes the trace of the whole run to the runs folder of its slot (`storage/mangosd/logs/runs/` of the realm folder). The player starts the full runs.

Type the path of that `.trace.txt` file in **Run trace** and click **Load walk**, or open the page with `&trace=<path>` in the address. The page draws where each group member walked, in its own colour. The **Walked path** box turns the layer on and off. Compare it with the route: look for places where the group left the line, cut a corner, or pulled a skipped pack.
