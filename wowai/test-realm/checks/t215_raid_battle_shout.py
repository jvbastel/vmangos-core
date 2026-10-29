#!/usr/bin/env python3
"""In-game check of jvbastel/WOWAI#215: raid-wide Battle Shout under the Forever rules (spec #204).

    # A slot with the Forever rules:
    wowai/test-realm/realm.sh --ticket N acquire --forever
    wowai/test-realm/realm.sh --ticket N check wowai/test-realm/checks/t215_raid_battle_shout.py forever
    # The same slot without them:
    wowai/test-realm/realm.sh --ticket N acquire
    wowai/test-realm/realm.sh --ticket N check wowai/test-realm/checks/t215_raid_battle_shout.py vanilla

Four level 60 player characters (no bots) make a raid: the Warrior and a party member in subgroup 1,
a near raid member and a far raid member in subgroup 2. The near members stand on the Warrior, the far
member 60 yards away (Battle Shout reaches 20 yards). The Warrior casts Battle Shout rank 7 (25289) with
CMSG_CAST_SPELL, as the action bar does. Each character then saves, and character_aura shows who has it.

forever: the Warrior, the party member and the near raid member of the other subgroup get Battle Shout.
         The far raid member does not.
vanilla: the Warrior and the party member get it. The raid members of the other subgroup do not.
"""
import struct
import sys

from kit import Realm, Results, pos

CMSG_GROUP_INVITE = 110
CMSG_GROUP_ACCEPT = 114
CMSG_GROUP_DISBAND = 123
CMSG_GROUP_CHANGE_SUB_GROUP = 638
CMSG_GROUP_RAID_CONVERT = 654
CMSG_CAST_SPELL = 302

BATTLE_SHOUT_7 = 25289
LEVEL = 60
FAR = 60.0

mode = sys.argv[1] if len(sys.argv) > 1 else ""
if mode not in ("forever", "vanilla"):
    sys.exit("usage: t215_raid_battle_shout.py forever|vanilla")

realm = Realm()
results = Results()

# (account, character): the Warrior, then the members by their place in the raid.
CAST = [("PLAYER215W", "Shoutwarrior"), ("PLAYER215P", "Shoutparty"), ("PLAYER215N", "Shoutnear"),
        ("PLAYER215F", "Shoutfar")]
clients = [realm.login(account, name, race=2, cls=1) for account, name in CAST]
warrior, party, near, far = clients
names = [name for _, name in CAST]


def pump_all(seconds):
    for c in clients:
        c.pump(seconds / len(clients))


# A clean start: no group of an earlier run, level 60, no Battle Shout.
for c in clients:
    c.send(CMSG_GROUP_DISBAND)
pump_all(2)
for c in clients:
    level = int(realm.sql("characters", "SELECT level FROM characters WHERE guid = %d" % c.guid) or 1)
    if level < LEVEL:
        c.gm(".levelup %d" % (LEVEL - level), select=c.guid)
    c.gm(".unaura %d" % BATTLE_SHOUT_7, select=c.guid)

# The raid: invite, accept, convert, then the near and far members to subgroup 2 (index 1).
for c, name in zip(clients[1:], names[1:]):
    warrior.send(CMSG_GROUP_INVITE, name.encode() + b"\0")
    pump_all(1)
    c.send(CMSG_GROUP_ACCEPT)
    pump_all(1)
warrior.send(CMSG_GROUP_RAID_CONVERT)
pump_all(1)
for name in names[2:]:
    warrior.send(CMSG_GROUP_CHANGE_SUB_GROUP, name.encode() + b"\0" + struct.pack("<B", 1))
    pump_all(1)


def subgroup(c):
    row = realm.sql("characters", "SELECT subgroup FROM group_member WHERE member_guid = %d" % c.guid)
    return int(row) if row else -1


groups = [subgroup(c) for c in clients]
results.check("the raid has the Warrior and the party member in subgroup 1, the others in subgroup 2",
              groups == [0, 0, 1, 1], "subgroups %r" % groups)

# The places: the near members on the Warrior, the far member 60 yards away.
x, y, z = pos(warrior, warrior.guid)
for c in (party, near):
    c.gm(".go xyz %f %f %f" % (x, y, z), select=c.guid)
far.gm(".go xyz %f %f %f" % (x + FAR, y, z + 2), select=far.guid)
pump_all(3)

# The cast: Battle Shout rank 7 with 100 Rage, no target (target mask 0, the caster).
warrior.gm(".learn %d" % BATTLE_SHOUT_7, select=warrior.guid)
warrior.gm(".modify rage 100", select=warrior.guid)
warrior.send(CMSG_CAST_SPELL, struct.pack("<IH", BATTLE_SHOUT_7, 0))
pump_all(3)


def has_shout(c):
    c.gm(".save", select=c.guid)
    rows = realm.sql("characters", "SELECT spell FROM character_aura WHERE guid = %d" % c.guid)
    return BATTLE_SHOUT_7 in {int(row) for row in rows.splitlines() if row}


got = dict(zip(("warrior", "party", "near", "far"), (has_shout(c) for c in clients)))
results.check("the Warrior has Battle Shout", got["warrior"], "%r" % got)
results.check("the party member has Battle Shout", got["party"], "%r" % got)
if mode == "forever":
    results.check("the near raid member of the other subgroup has Battle Shout", got["near"], "%r" % got)
else:
    results.check("the near raid member of the other subgroup has no Battle Shout", not got["near"], "%r" % got)
results.check("the raid member out of range has no Battle Shout", not got["far"], "%r" % got)

for c in clients:
    c.send(CMSG_GROUP_DISBAND)
pump_all(1)
results.finish()
