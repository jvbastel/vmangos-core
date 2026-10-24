# Gear templates

A gear template is the pool of items that a character of one class and role
can wear at each level. mangosd reads every `*.txt` file of this folder at
startup (`<Bots.DataDir>/gear/`). See step 4 of the version 1 build spec.

- Generated templates: `<class>_<role>.txt`, written by the generator in
  `bots/tools/gear/` and reviewed by a person.
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
