#include "Strategy.h"

#include <algorithm>

namespace Bots
{
    void StrategyStack::Push(std::shared_ptr<Strategy const> strategy)
    {
        m_strategies.push_back(std::move(strategy));
    }

    bool StrategyStack::Remove(std::string const& name)
    {
        auto found = std::find_if(m_strategies.begin(), m_strategies.end(),
            [&name](std::shared_ptr<Strategy const> const& strategy) { return strategy->name == name; });
        if (found == m_strategies.end())
            return false;
        m_strategies.erase(found);
        return true;
    }

    bool StrategyStack::Contains(std::string const& name) const
    {
        for (auto const& strategy : m_strategies)
            if (strategy->name == name)
                return true;
        return false;
    }

    std::vector<std::string> StrategyStack::Names() const
    {
        std::vector<std::string> names;
        for (auto const& strategy : m_strategies)
            names.push_back(strategy->name);
        return names;
    }

    std::string StrategyStack::Think(BotContext const& context) const
    {
        // The rules in the order of the tie rules: top of the stack first,
        // and in one strategy the first rule first. A stable sort by priority
        // then keeps that order on a tie.
        std::vector<Rule const*> rules;
        for (auto strategy = m_strategies.rbegin(); strategy != m_strategies.rend(); ++strategy)
            for (Rule const& rule : (*strategy)->rules)
                rules.push_back(&rule);
        std::stable_sort(rules.begin(), rules.end(),
            [](Rule const* a, Rule const* b) { return a->priority > b->priority; });

        for (Rule const* rule : rules)
        {
            if (!rule->trigger(context) || !rule->action.canDo(context))
                continue;
            rule->action.run(context);
            return rule->name;
        }
        return "";
    }
}
