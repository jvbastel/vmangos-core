# Gear templates

A gear template is the pool of items that a character of one class and role
can wear at each level. mangosd reads every `*.txt` file of this folder at
startup (`<Bots.DataDir>/gear/`). See step 4 of the version 1 build spec.

- Generated templates: `<class>_<role>.txt`, written by the generator in
  `bots/tools/gear/` and reviewed by a person. Do not edit them by hand.
- Hand-made templates: `<name>.txt`, from the template builder page or by hand.

The full rules are in `src/bots/GearTemplate.h` (the format) and
`src/bots/GearDraw.h` (the draw). In short:

```
# Comments start with # and run to the end of the line.
template warrior_tank warrior tank generated
shape sword-and-board mainhand shield
item head 1001 14-20 0 31.5      # A helm
item finger 1002 12-18 0 12      # A ring
item mainhand 1004 12-18 0 40.25 # A one-handed sword
item shield 1005 10-16 0 22      # A shield
```

- `template <name> <class> <role> generated|hand` is the first line. The name
  is the file name without `.txt`.
- `item <slot> <entry> <minLevel>-<maxLevel> <minPatch> <score>`. A hand-made
  item has no score. `<minPatch>` counts like `WowPatch`: 0 = 1.2 ... 10 = 1.12.
- An `item` line may end with `horde` or `alliance`: only that faction can
  use and get the item, and only its characters draw or wear it. A line with
  no faction word is for both factions. One generated template per class and
  role holds the items of both factions. The generator works out the lines of
  each faction on their own (pool, blue caps, fill), so the Alliance items
  never change what a Horde character draws.
- Slots: `head neck shoulders back chest shirt tabard wrists hands waist legs
  feet finger trinket ranged`, and the weapon slots `mainhand offhand twohand
  shield holdable`. Finger and trinket hold two different items.
- `shape <name> <weaponSlot> [<weaponSlot>]` (generated only): one weapon
  shape that the class and role may use. The draw picks one shape first.
- A generated template draws per slot from the top 10 by score, weighted
  toward the top, with a seed from the character GUID. A hand-made template
  has no draw: each item of the level is worn, and an empty slot stays empty.

GM commands (console and SOAP too):

- `.wowai bot create <name> <class> <role> [race] [gear <template>]`
- `.wowai gear equip <character> [template]` destroys the equipped items of a
  character in the world and equips it from the template. With no template, a
  bot wears the generated template of its class and role.

## The generator

`bots/tools/gear/generate.py` reads the world database through a read-only
MySQL user and writes the generated templates of the roles in
`bots/tools/gear/settings.toml`. The settings file holds the level, source,
and quality numbers, the weapon shapes and armor types of each role, and the
stat weights. `bots/tools/gear/bans.txt` (`ban <entry>` lines) removes items
from every generated template. `gear_data.py` is the item data module that
the generator and the template builder page share.

To make the files again from a test realm (`wowai/dev realm up` first):

```
bots/tools/gear/generate-from-realm        # all roles, into this folder
git diff --stat bots/data/gear/            # review, then commit
```

## The template builder page

`bots/tools/gear/builder_web.py` is a small web page that builds hand-made
templates. It has a search per slot (filters for required level, quality,
source, content patch, and armor type) that shows the score, stats, and
sources of each item, a paste box for item IDs and Wowhead item links, and
the levels at which each slot is empty. It checks a template as mangosd
does and writes only hand-made templates into this folder: it refuses the
`<class>_<role>` names and never changes a generated file.

```
bots/tools/gear/builder-from-realm         # then open http://127.0.0.1:8765/
```

It reads the item data once from the test realm (about 20 s), with the same
read-only user as the generator (`with-realm-db`).

For item icons, run the one-time icon script once against a local 1.12
client. It only reads the client archives (`ItemDisplayInfo.dbc` and the
BLP icons) and writes PNG files to `bots/tools/gear/icon-cache/`, which git
ignores (about 2,200 icons, 17 MB, 10 s):

```
cd bots/tools/gear && uv run icon_cache.py /mnt/c/Games/WoW-1.12.1
```

The page then shows the icons (`--icons DIR` takes another cache folder).
Without the cache, it shows item names in the color of their quality. About
50 old display rows name icons that are not in the client; their items show
text only.

It needs `uv`. The tests: `cd bots/tools/gear && uv run pytest`, and
`wowai/dev test` (GearDataTest reads the files of this folder). mangosd drops
the items of a later content patch than the realm's `WowPatch` with no
message.
