// Tests of the pure rules of Victory Rush under the Forever rules (VictoryRushRules.h, jvbastel/WOWAI#213).
// No GoogleTest and no core: wowai/tests/forever-victory-rush.test.sh builds this file with the host
// compiler and runs it. The glob of src/game/Forever does not take this subfolder into the core.

#include "../VictoryRushRules.h"

#include <cstdio>

namespace
{
    int failures = 0;
    int checks = 0;

    void Check(char const* name, bool ok)
    {
        ++checks;
        if (!ok)
            ++failures;
        std::printf("%s: %s\n", ok ? "PASS" : "FAIL", name);
    }

    void Equal(char const* name, long actual, long expected)
    {
        ++checks;
        if (actual != expected)
        {
            ++failures;
            std::printf("FAIL: %s: %ld, expected %ld\n", name, actual, expected);
        }
        else
            std::printf("PASS: %s\n", name);
    }

    Forever::VictoryRushRules::Kill QualifyingKill()
    {
        Forever::VictoryRushRules::Kill kill;
        kill.rulesOn = true;
        kill.killerIsWarriorPlayer = true;
        kill.killerKnowsVictoryRush = true;
        kill.killedSelf = false;
        kill.honorOrXpTarget = true;
        return kill;
    }
}

int main()
{
    using namespace Forever::VictoryRushRules;

    // Who gets Victorious.
    Check("a Forever Warrior who kills an enemy that yields experience gets Victorious",
          GrantsVictorious(QualifyingKill()));
    Kill kill = QualifyingKill();
    kill.rulesOn = false;
    Check("with the rules off nobody gets it", !GrantsVictorious(kill));
    kill = QualifyingKill();
    kill.honorOrXpTarget = false;
    Check("a grey (trivial) enemy gives nothing", !GrantsVictorious(kill));
    kill = QualifyingKill();
    kill.killerIsWarriorPlayer = false;
    Check("another class or a creature gets nothing", !GrantsVictorious(kill));
    kill = QualifyingKill();
    kill.killerKnowsVictoryRush = false;
    Check("a Warrior below level 20 (no Victory Rush yet) gets nothing", !GrantsVictorious(kill));
    kill = QualifyingKill();
    kill.killedSelf = true;
    Check("killing yourself gives nothing", !GrantsVictorious(kill));

    // Damage and heal (client spell 402927: 1 + 15% of attack power, heals 10% of max health).
    Equal("1000 attack power: 1 + 150 damage", Damage(1, 1000.0f, 15), 151);
    Equal("no attack power: the flat 1", Damage(1, 0.0f, 15), 1);
    Equal("fractions round down", Damage(1, 99.0f, 15), 15);
    Equal("4000 max health: heals 400", Heal(4000, 10), 400);
    Equal("a tiny pool heals at least 1", Heal(5, 10), 1);

    std::printf("%d of %d checks passed.\n", checks - failures, checks);
    return failures == 0 ? 0 : 1;
}
