#!/usr/bin/env python3
"""In-game check of ticket #75 (caster cast checks), on the check kit.

    wowai/test-realm/realm.sh --ticket N acquire
    wowai/test-realm/realm.sh --ticket N check wowai/test-realm/checks/t75_cast_checks.py

A new level 1 player, Lowbieson, invites the level 16 Priest healer Hela.

1. Rank by level: Hela buffs the level 1 player with Power Word: Fortitude,
   with no SPELL_FAILED_LOWLEVEL (rank 2 needs a target of level 2 or more).
2. Stun: the player is hurt while Hela is frozen (a stun). Hela does not try
   to heal: no SPELL_FAILED_STUNNED. Unfrozen, she heals.
3. Silence: the same with a silence. No SPELL_FAILED_SILENCED.
"""
import re

from kit import Realm, Results, stat
from wowclient import CHAT_MSG_PARTY, CHAT_MSG_WHISPER

ACCOUNT = "PLAYER75"
PLAYER = "Lowbieson"
PRIEST = "Hela"
FREEZE = 9454  # A stun without an end.
SILENCE = 6726
HEALS = r"%s: (heal|lesser heal|lesser heal a badly hurt member|renew|shield a member near death)\b" % PRIEST

realm = Realm()
results = Results()
priest_guid = realm.guid_of(PRIEST)
c = realm.login(ACCOUNT, PLAYER)
realm.mark("start")
c.gm(".gm off")
level = realm.sql("characters", "SELECT level FROM characters WHERE name = '%s'" % PLAYER)
results.check("the player is level 1", level == "1", "level %s" % level)


def hurt_player():
    # Without the Renew of an earlier heal, which would heal it back.
    c.gm(".unaura 139", select=c.guid, wait=0.5)
    _, max_health, _, _ = stat(c, c.guid)
    c.gm(".modify hp %d %d" % (max(1, int(max_health * 0.3)), max_health), select=c.guid)


def heal_player():
    _, max_health, _, _ = stat(c, c.guid)
    c.gm(".modify hp %d %d" % (max_health, max_health), select=c.guid)


whole = []  # The log of each part, for the check of the whole run.

# The group, out of the way of the guards of the Den.
c.chat(CHAT_MSG_WHISPER, "invite", target=PRIEST)
c.pump(4)
c.gm(".revive %s" % PRIEST)
c.gm(".namego %s" % PRIEST, wait=2.0)
c.chat(CHAT_MSG_PARTY, "follow")
c.pump(3)
whole.append(realm.log())

# 1. Fortitude on the level 1 player.
realm.mark("rank")
c.gm(".unaura 1243", select=c.guid)
buffed = realm.wait_log(r"%s: fortitude" % PRIEST, 25, c)
c.pump(8)
text = realm.log()
whole.append(text)
results.check("Hela buffs the level 1 player with Fortitude", buffed, repr(re.findall(r"%s: [a-z ]+" % PRIEST, text)[-10:]))
results.check("no SPELL_FAILED_LOWLEVEL", "SPELL_FAILED_LOWLEVEL" not in text,
              repr([line for line in text.splitlines() if "LOWLEVEL" in line][:3]))
tries = len(re.findall(r"%s: fortitude" % PRIEST, text))
results.check("Hela does not try Fortitude again and again", tries <= 3, "%d tries" % tries)

# 2. Stun.
realm.mark("stun")
c.gm(".aura %d" % FREEZE, select=priest_guid)
c.pump(1)
hurt_player()
c.pump(10)
text = realm.log()
results.check("no SPELL_FAILED_STUNNED while Hela is stunned", "SPELL_FAILED_STUNNED" not in text,
              repr([line for line in text.splitlines() if "STUNNED" in line][:3]))
results.check("Hela does not try to heal while stunned", re.search(HEALS, text) is None,
              repr(re.findall(HEALS, text)[:5]))
c.gm(".unaura %d" % FREEZE, select=priest_guid)
hurt_player()
healed = realm.wait_log(HEALS, 15, c)
results.check("Hela heals once the stun ends", healed, repr(re.findall(r"%s: [a-z ]+" % PRIEST, realm.log())[-5:]))
c.pump(6)
whole.append(realm.log())
heal_player()
c.pump(4)

# 3. Silence: kept on by a new aura each few seconds.
realm.mark("silence")
c.gm(".aura %d" % SILENCE, select=priest_guid, wait=0.5)
hurt_player()
for _ in range(4):
    c.gm(".aura %d" % SILENCE, select=priest_guid, wait=0.5)
    c.pump(2)
text = realm.log()
results.check("no SPELL_FAILED_SILENCED while Hela is silenced", "SPELL_FAILED_SILENCED" not in text,
              repr([line for line in text.splitlines() if "SILENCED" in line][:3]))
results.check("Hela does not try to heal while silenced", re.search(HEALS, text) is None,
              repr(re.findall(HEALS, text)[:5]))
c.gm(".unaura %d" % SILENCE, select=priest_guid)
heal_player()
c.pump(4)
whole.append(realm.log())

# The whole run.
failed = [line for line in "".join(whole).splitlines() if re.search(r"SPELL_FAILED_(LOWLEVEL|STUNNED)", line)]
results.check("the whole run has no SPELL_FAILED_LOWLEVEL and no SPELL_FAILED_STUNNED line", not failed, repr(failed[:3]))
results.finish()
