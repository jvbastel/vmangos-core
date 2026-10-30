#!/usr/bin/env python3
"""In-game check of jvbastel/WOWAI#211: data-only Forever talents give their effect on the character.

    wowai/test-realm/realm.sh --ticket N acquire --forever
    wowai/test-realm/realm.sh --ticket N check wowai/test-realm/checks/t211_forever_data_talents.py forever
    # The same slot without the Forever rules (the stock values):
    wowai/test-realm/realm.sh --ticket N acquire
    wowai/test-realm/realm.sh --ticket N check wowai/test-realm/checks/t211_forever_data_talents.py vanilla

A level 60 player Warrior learns talents with CMSG_LEARN_TALENT, as the talent window does. A spell
modifier talent (aura 107/108) makes the server send the client the total modifier of each family flag bit
(SMSG_SET_FLAT/PCT_SPELL_MODIFIER: uint8 bit, uint8 op, int32 value), so the check reads the effect that
the character has. The values come from forever/warrior.tsv (client build 1.60.1.70685).

Arms:       Improved Rend 1/3: Rend (bit 5) bleed +12% (op 8, all effects). Stock +15%.
Fury:       Booming Voice 1/5: shouts (bits 16-19) radius +10% (op 6) and Rage cost -5% (op 14). Stock: no
            cost modifier.
Protection: Improved Bloodrage 2/2 (+50% of all Bloodrage Rage): Bloodrage gives 15 Rage at once (150 in
            tenths) and its Rage over time (29131, which now has Bloodrage's family flag) 1.5 Rage per tick
            (15; 10 without the talent).
            Focused Rage 3/3 (talent 60008, tier 5): Rage cost -3 (op 14, -30) on Pummel (bit 3), the
            Demoralizing Shout (bit 17) and Piercing Howl (bit 20); never on Battle Shout (bit 16).
vanilla:    Improved Rend 1/3 is +15%, Booming Voice has no cost modifier and talent 60008 is refused.
"""
import struct
import sys

from kit import Realm, Results
from wowclient import SpellClient, unpack_guid

SMSG_SET_FLAT_SPELL_MODIFIER = 614
SMSG_SET_PCT_SPELL_MODIFIER = 615
SMSG_PERIODICAURALOG = 590
SMSG_SPELLENERGIZELOG = 337
SPELL_AURA_PERIODIC_ENERGIZE = 24
POWER_RAGE = 1

SPELLMOD_RADIUS, SPELLMOD_ALL_EFFECTS, SPELLMOD_COST = 6, 8, 14
BIT_PUMMEL, BIT_REND, BIT_BATTLE_SHOUT, BIT_DEMORALIZING_SHOUT, BIT_PIERCING_HOWL = 3, 5, 16, 17, 20

IMPROVED_REND, BOOMING_VOICE, IMPROVED_BLOODRAGE = 127, 158, 142
# The Protection path to tier 5: (talent, ranks).
PROTECTION = [(IMPROVED_BLOODRAGE, 2), (1601, 3),   # Shield Specialization: 5 points
              (138, 5),                             # Anticipation: 10
              (153, 1), (151, 3), (144, 1),         # Last Stand, Improved Disarm, Defiance: 15
              (146, 3), (149, 2)]                   # Improved Sunder Armor, Improved Shield Bash: 20
FOCUSED_RAGE = 60008
BLOODRAGE, BLOODRAGE_OVER_TIME = 2687, 29131
LEVEL = 60


class TalentClient(SpellClient):
    """Keeps the learned spells, the spell modifier totals, and the Rage log of the character."""

    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        self.mods = {}         # (flat or pct, bit, op) -> value
        self.energize = []     # (spell, amount)
        self.ticks = []        # (spell, amount)

    def _observe(self, opcode, body):
        super()._observe(opcode, body)
        if opcode in (SMSG_SET_FLAT_SPELL_MODIFIER, SMSG_SET_PCT_SPELL_MODIFIER):
            bit, op, value = struct.unpack_from("<BBi", body, 0)
            self.mods["flat" if opcode == SMSG_SET_FLAT_SPELL_MODIFIER else "pct", bit, op] = value
        elif opcode == SMSG_SPELLENERGIZELOG:
            target, pos = unpack_guid(body, 0)
            _, pos = unpack_guid(body, pos)
            spell, power, amount = struct.unpack_from("<III", body, pos)
            if target == self.guid and power == POWER_RAGE:
                self.energize.append((spell, amount))
        elif opcode == SMSG_PERIODICAURALOG:
            target, pos = unpack_guid(body, 0)
            _, pos = unpack_guid(body, pos)
            spell, _, aura = struct.unpack_from("<III", body, pos)
            if target == self.guid and aura == SPELL_AURA_PERIODIC_ENERGIZE:
                power, amount = struct.unpack_from("<II", body, pos + 12)
                if power == POWER_RAGE:
                    self.ticks.append((spell, amount))


mode = sys.argv[1] if len(sys.argv) > 1 else ""
if mode not in ("forever", "vanilla"):
    sys.exit("usage: t211_forever_data_talents.py forever|vanilla")

realm = Realm()
results = Results()
name = "Foreverdata" if mode == "forever" else "Vanilladata"
c = realm.login("PLAYER211", name, race=2, cls=1, client_class=TalentClient)
me = c.guid
level = int(realm.sql("characters", "SELECT level FROM characters WHERE guid = %d" % me) or 1)
if level < LEVEL:
    c.gm(".levelup %d" % (LEVEL - level), select=me)
c.gm(".reset talents", select=me)
c.pump(1)
c.mods.clear()

# Arms
rend = c.learn_talent(IMPROVED_REND)
results.check("Arms: Improved Rend 1/3 is learned", rend == [12286], "learned %r" % rend)
expected = 12 if mode == "forever" else 15
got = c.mods.get(("pct", BIT_REND, SPELLMOD_ALL_EFFECTS))
results.check("Arms: Improved Rend 1/3 raises the Rend bleed by %d%%" % expected, got == expected, "got %r" % got)

# Fury
voice = c.learn_talent(BOOMING_VOICE)
results.check("Fury: Booming Voice 1/5 is learned", voice == [12321], "learned %r" % voice)
shout_cost = [c.mods.get(("pct", bit, SPELLMOD_COST)) for bit in range(16, 20)]
if mode == "forever":
    radius = [c.mods.get(("pct", bit, SPELLMOD_RADIUS)) for bit in range(16, 20)]
    results.check("Fury: Booming Voice 1/5 widens every shout by 10%", radius == [10] * 4, "got %r" % radius)
    results.check("Fury: Booming Voice 1/5 makes every shout 5% cheaper", shout_cost == [-5] * 4, "got %r" % shout_cost)
else:
    results.check("Fury: stock Booming Voice has no cost modifier", shout_cost == [None] * 4, "got %r" % shout_cost)

# Protection
if mode == "forever":
    path = {talent: c.learn_ranks(talent, ranks) for talent, ranks in PROTECTION}
    results.check("Protection: the 20 points up to tier 5 are learned",
                  all(len(spells) == ranks for (talent, ranks), spells in zip(PROTECTION, path.values())),
                  "learned %r" % path)
    focused = c.learn_ranks(FOCUSED_RAGE, 3)
    results.check("Protection: Focused Rage 3/3 is learned", focused == [60120, 60121, 60122], "learned %r" % focused)
    cost = {bit: c.mods.get(("flat", bit, SPELLMOD_COST))
            for bit in (BIT_PUMMEL, BIT_DEMORALIZING_SHOUT, BIT_PIERCING_HOWL, BIT_BATTLE_SHOUT)}
    results.check("Protection: Focused Rage 3/3 takes 3 Rage off Pummel, Demoralizing Shout and Piercing Howl",
                  [cost[BIT_PUMMEL], cost[BIT_DEMORALIZING_SHOUT], cost[BIT_PIERCING_HOWL]] == [-30] * 3, "got %r" % cost)
    results.check("Protection: Focused Rage leaves Battle Shout alone", not cost[BIT_BATTLE_SHOUT], "got %r" % cost)

    c.gm(".learn %d" % BLOODRAGE, select=me)
    c.gm(".cooldown", select=me)
    c.energize.clear()
    c.ticks.clear()
    c.cast_at(BLOODRAGE)
    c.pump(4)
    instant = [amount for spell, amount in c.energize if spell == BLOODRAGE]
    ticks = [amount for spell, amount in c.ticks if spell == BLOODRAGE_OVER_TIME]
    results.check("Protection: Bloodrage with Improved Bloodrage 2/2 gives 15 Rage at once", instant == [150],
                  "got %r" % c.energize)
    results.check("Protection: its Rage over time gives 1.5 Rage per tick (stock 1)",
                  bool(ticks) and set(ticks) == {15}, "got %r" % c.ticks)
else:
    focused = c.learn_talent(FOCUSED_RAGE)
    results.check("the vanilla trees have no talent 60008 (Focused Rage)", focused == [], "learned %r" % focused)

results.finish()
