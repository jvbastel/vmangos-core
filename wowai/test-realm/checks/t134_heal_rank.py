#!/usr/bin/env python3
"""In-game check of ticket #134 (heals pick their rank by missing health), on
the check kit.

    wowai/test-realm/realm.sh --ticket N acquire
    wowai/test-realm/realm.sh --ticket N check wowai/test-realm/checks/t134_heal_rank.py

A new player, Rankson, invites the level 16 warrior tank Grukk and the level
16 Priest healer Hela. The client reads the SMSG_SPELL_GO of each cast of
Hela, so the check sees the rank (the spell ID) that she casts.

1. Small wound: Grukk at 130 of 200 health (70 missing, under the Lesser Heal
   fraction 0.7), with a Renew on already. Hela casts a low rank of Lesser
   Heal: rank 2 (2052, about 84 health), not rank 3 (2053).
2. Big wound: Grukk at 40% of his health. Hela casts Heal, rank 1 (2054),
   the highest rank that she knows.
"""
import struct

from kit import Realm, Results, stat
from wowclient import CHAT_MSG_PARTY, CHAT_MSG_WHISPER, WorldClient, unpack_guid

ACCOUNT = "PLAYER134"
PLAYER = "Rankson"
PRIEST = "Hela"
TANK = "Grukk"
SMSG_SPELL_GO = 306
RENEW = 139
LESSER_HEAL = {2050: 1, 2052: 2, 2053: 3}
HEAL = {2054: 1, 2055: 2}


class SpellClient(WorldClient):
    """Keeps (caster, spell, first hit target) of each SMSG_SPELL_GO."""

    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        self.spell_log = []

    def _observe(self, opcode, body):
        super()._observe(opcode, body)
        if opcode != SMSG_SPELL_GO:
            return
        try:
            _, pos = unpack_guid(body, 0)
            caster, pos = unpack_guid(body, pos)
            spell = struct.unpack_from("<I", body, pos)[0]
            hits = body[pos + 6]
            target = struct.unpack_from("<Q", body, pos + 7)[0] if hits else 0
            self.spell_log.append((caster, spell, target))
        except (IndexError, struct.error):
            pass


realm = Realm()
results = Results()
priest = realm.guid_of(PRIEST)
tank = realm.guid_of(TANK)
c = realm.login(ACCOUNT, PLAYER, client_class=SpellClient)
c.gm(".gm off")

for bot in (TANK, PRIEST):
    c.chat(CHAT_MSG_WHISPER, "invite", target=bot)
    c.pump(4)
    c.gm(".revive %s" % bot)
    c.gm(".namego %s" % bot, wait=2.0)
c.chat(CHAT_MSG_PARTY, "stay")
# The buffs first: a new Fortitude would set the max health of the tank back.
c.pump(20)


def heals_on_tank(seconds, spells):
    """The first cast of Hela on the tank of one of `spells` within `seconds`."""
    start = len(c.spell_log)
    for _ in range(seconds * 2):
        c.pump(0.5)
        for caster, spell, target in c.spell_log[start:]:
            if caster == priest and target == tank and spell in spells:
                return spell
    return None


def hela_casts(start):
    return [(spell, hex(target)) for caster, spell, target in c.spell_log[start:] if caster == priest]


_, max_health, _, _ = stat(c, tank)
results.check("the tank has a max health", max_health > 0, "max %d" % max_health)

# 1. Small wound.
start = len(c.spell_log)
c.gm(".aura %d" % RENEW, select=tank, wait=0.5)
c.gm(".modify hp 130 200", select=tank)
spell = heals_on_tank(15, LESSER_HEAL)
results.check("a small wound on the tank gets a low rank of Lesser Heal (rank 2, 2052)", spell == 2052,
              "Lesser Heal rank %s; Hela casts %r" % (LESSER_HEAL.get(spell), hela_casts(start)))
c.pump(4)
c.gm(".unaura %d" % RENEW, select=tank, wait=0.5)
c.gm(".modify hp %d %d" % (max_health, max_health), select=tank)
c.pump(4)

# 2. Big wound.
start = len(c.spell_log)
c.gm(".modify hp %d %d" % (int(max_health * 0.4), max_health), select=tank)
spell = heals_on_tank(15, HEAL)
results.check("a big wound on the tank gets the highest rank of Heal (rank 1, 2054)", spell == 2054,
              "Heal rank %s; Hela casts %r" % (HEAL.get(spell), hela_casts(start)))
c.gm(".modify hp %d %d" % (max_health, max_health), select=tank)
c.pump(2)

text = realm.log()
results.check("no 'cannot cast' line of Hela", "%s cannot cast" % PRIEST not in text,
              repr([line for line in text.splitlines() if "cannot cast" in line][:3]))
results.finish()
