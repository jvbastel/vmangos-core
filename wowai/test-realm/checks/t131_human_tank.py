#!/usr/bin/env python3
"""In-game check of the review fix of spec #131, story 10 (the healer never
shields a human warrior tank), on the check kit.

    wowai/test-realm/realm.sh --ticket N acquire
    wowai/test-realm/realm.sh --ticket N check wowai/test-realm/checks/t131_human_tank.py

A new player, Shieldless (an Orc Warrior: rage), invites the Priest Hela and
the mage Ezzra, and no bot tank. With no bot tank, the group's tank is the
human leader (bots/GroupTank.h). The client reads the SMSG_SPELL_GO of each
cast of Hela.

1. The player near death (20% health), again and again: Hela heals him and
   never casts Power Word: Shield on him.
2. The mage near death: Hela still shields the mage.
"""
import struct

from kit import Realm, Results, stat
from wowclient import CHAT_MSG_PARTY, CHAT_MSG_WHISPER, WorldClient, unpack_guid

ACCOUNT = "PLAYER131"
PLAYER = "Shieldless"
PRIEST = "Hela"
MAGE = "Ezzra"
SMSG_SPELL_GO = 306
# Power Word: Shield ranks 1 to 3 (level 16).
SHIELDS = {17, 592, 600}
HEALS = {2050, 2052, 2053, 2054, 2055, 139, 6074, 6075}


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
mage = realm.guid_of(MAGE)
c = realm.login(ACCOUNT, PLAYER, race=2, cls=1, client_class=SpellClient)
me = realm.guid_of(PLAYER)
c.gm(".gm off")
c.gm(".levelup 15", select=me)

for bot in (PRIEST, MAGE):
    c.chat(CHAT_MSG_WHISPER, "invite", target=bot)
    c.pump(4)
    c.gm(".revive %s" % bot)
    c.gm(".namego %s" % bot, wait=2.0)
c.chat(CHAT_MSG_PARTY, "stay")
# The buffs first: a new Fortitude would set the max health back.
c.pump(20)


def hela_casts_on(target, start):
    return [spell for caster, spell, t in c.spell_log[start:] if caster == priest and t == target]


_, top, _, _ = stat(c, me)
results.check("the player has a max health", top > 0, "max %d" % top)

# 1. The human warrior tank near death.
realm.mark("player near death")
start = len(c.spell_log)
raised = 0
for _ in range(5):
    c.gm(".modify hp %d %d" % (max(1, top // 5), top), select=me, wait=0.5)
    c.pump(3.0)
    health = stat(c, me)[0]
    if health > top // 5:
        raised += 1
on_player = hela_casts_on(me, start)
results.check("Hela never shields the human warrior tank", not (set(on_player) & SHIELDS), "casts %r" % on_player)
results.check("Hela heals the human warrior tank near death", raised > 0 and bool(set(on_player) & HEALS),
              "health raised %d of 5 times, casts %r" % (raised, on_player))
c.gm(".modify hp %d %d" % (top, top), select=me, wait=0.5)
c.pump(3)

# 2. The mage near death: the shield still works.
_, mage_top, _, _ = stat(c, mage)
start = len(c.spell_log)
c.gm(".modify hp %d %d" % (max(1, mage_top // 5), mage_top), select=mage, wait=0.5)
shielded = False
for _ in range(8):
    c.pump(1)
    if set(hela_casts_on(mage, start)) & SHIELDS:
        shielded = True
        break
results.check("Hela still shields the mage near death", shielded, "casts %r" % hela_casts_on(mage, start))
c.gm(".modify hp %d %d" % (mage_top, mage_top), select=mage, wait=0.5)

text = realm.log()
results.check("no 'cannot cast' line of Hela", "%s cannot cast" % PRIEST not in text,
              repr([line for line in text.splitlines() if "%s cannot cast" % PRIEST in line][:3]))
results.finish()
