#include "TaragamanReaction.h"

namespace Bots
{
    namespace Taragaman
    {
        std::shared_ptr<Strategy const> MakeReaction(BotRole role)
        {
            std::vector<Rule> rules;
            (void)role;
            return std::make_shared<Strategy const>(Strategy{"taragaman the hungerer", std::move(rules)});
        }
    }
}
