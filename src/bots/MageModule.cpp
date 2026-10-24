#include "MageModule.h"
#include "CommonRules.h"
#include "DamageRules.h"

namespace Bots
{
    namespace Mage
    {
        std::shared_ptr<Strategy const> MakeFrostStrategy()
        {
            int const P = Priority::Class;
            std::vector<Rule> rules = {
                CloseInRule(P + 70),
                CastRule("frostbolt", P + 60, MayAttackFocus, Frostbolt, PickFocusTarget),
                // Below level 4, and against a target that Frostbolt cannot
                // hit, for example one immune to frost.
                CastRule("fireball", P + 50, MayAttackFocus, Fireball, PickFocusTarget),
            };
            return std::make_shared<Strategy const>(Strategy{"mage frost", std::move(rules)});
        }
    }
}
