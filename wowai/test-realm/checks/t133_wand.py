#!/usr/bin/env python3
"""In-game check of ticket #133 (the healer shoots its wand between heals), on
the check kit.

    wowai/test-realm/realm.sh --ticket N acquire
    wowai/test-realm/runs.py --ticket N --watch 81NN scenario two_packs 1 &
    wowai/test-realm/realm.sh --ticket N check wowai/test-realm/checks/t133_wand.py

While the scripted run fights its two packs, the GM player Wandson stands
near the Priest Hela (GM mode: the enemies do not see him). His client reads
the SMSG_SPELL_GO of each cast of Hela, and he reads her mana about once a
second. Hela wears a wand (gear mage_ranged.txt, Opaque Wand at level 16).

1. Hela shoots: Shoot (5019) goes off on an enemy.
2. She shoots between heals: a Shoot after a heal of the fight.
3. Spirit regen goes on while she shoots: in a stretch of shots with no
   mana spell for 5 seconds (the 5-second rule), her mana rises.
"""
import re
import struct
import time

from kit import Realm, Results, stat
from wowclient import WorldClient, unpack_guid

ACCOUNT = "PLAYER133"
PLAYER = "Wandson"
PRIEST = "Hela"
SMSG_SPELL_GO = 306
SHOOT = 5019
# The spells of Hela that cost no mana: Shoot. Every other cast of hers
# starts the 5-second rule.
FREE = {SHOOT}
FIVE_SECOND_RULE = 5.0


class SpellClient(WorldClient):
    """Keeps (time, caster, spell, first hit target) of each SMSG_SPELL_GO."""

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
            self.spell_log.append((time.time(), caster, spell, target))
        except (IndexError, struct.error):
            pass


realm = Realm()
results = Results()
priest = realm.guid_of(PRIEST)
c = realm.login(ACCOUNT, PLAYER, client_class=SpellClient)

# The run fights: its status names the fight step.
fighting = False
for _ in range(180):
    if re.search(r"running, .*fight ", realm.soap(".wowai run status")):
        fighting = True
        break
    c.pump(1)
results.check("the scripted run fights its packs", fighting, realm.soap(".wowai run status"))
c.gm(".gm on")
c.gm(".goname %s" % PRIEST, wait=3.0)
realm.mark("fight")

# Mana samples (time, mana) until the run ends.
mana = []
for _ in range(240):
    m = stat(c, priest)[2]
    if m >= 0:
        mana.append((time.time(), m))
    if "running" not in realm.soap(".wowai run status"):
        break
    c.pump(0.2)
status = realm.soap(".wowai run status")

casts = [(t, spell) for t, caster, spell, _ in c.spell_log if caster == priest]
shots = [t for t, spell in casts if spell == SHOOT]
heals = [t for t, spell in casts if spell not in FREE]
start = casts[0][0] if casts else time.time()
trace = ["%.1f %d" % (t - start, spell) for t, spell in casts]
results.check("Hela shoots her wand (Shoot 5019)", len(shots) > 0, "%d shots; casts %r" % (len(shots), trace[:40]))
results.check("Hela shoots again after a heal", any(s > h for h in heals for s in shots),
              "heals at %r, shots at %r" % (["%.1f" % (h - start) for h in heals], ["%.1f" % (s - start) for s in shots][:20]))


def last_mana_cast(before):
    earlier = [h for h in heals if h <= before]
    return earlier[-1] if earlier else None


# Spirit regen: two samples a and b, both 5 s past the last mana cast, no mana
# cast between them, a shot between them, and more mana at b.
regen = []
for (ta, ma), (tb, mb) in zip(mana, mana[1:]):
    last = last_mana_cast(tb)
    out_of_rule = last is None or ta - last >= FIVE_SECOND_RULE
    shot_between = any(ta - 1.0 <= s <= tb for s in shots)
    if out_of_rule and shot_between:
        regen.append(mb - ma)
results.check("Hela's mana rises while she shoots, outside the 5-second rule", any(d > 0 for d in regen),
              "mana steps while shooting %r; samples %d" % (regen[:20], len(mana)))

text = realm.log()
rules = re.findall(r"\[Bots\] %s: ([a-z ]+)" % PRIEST, text)
results.check("the wand rule fires", "shoot the focus target" in rules, repr(sorted(set(rules))))
results.check("no 'cannot cast' line of Hela", "%s cannot cast" % PRIEST not in text,
              repr([line for line in text.splitlines() if "%s cannot cast" % PRIEST in line][:3]))
results.check("the run passes", "passed" in status, status)
print("stop the shot fired %d times" % rules.count("stop the shot"))
results.finish()
