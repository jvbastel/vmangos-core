#ifndef WOWAI_BOTS_TESTS_FAKE_BOT_H
#define WOWAI_BOTS_TESTS_FAKE_BOT_H

#include "BotActions.h"
#include "BotView.h"

#include <iomanip>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace Bots
{
    namespace Tests
    {
        // The fake adapter of the tests. A test sets the world that the bot
        // sees, and reads the deeds of the bot as text lines, in order.
        class FakeBot : public BotView, public BotActions
        {
        public:
            explicit FakeBot(Guid self) : m_self(self) {}

            void SetHealth(Guid unit, std::uint32_t current, std::uint32_t max)
            {
                m_health[unit] = Health{current, max};
            }
            void SetCurrentCast(Guid unit, SpellId spell) { m_casts[unit] = spell; }

            std::vector<std::string> const& Deeds() const { return m_deeds; }

            // BotView
            Guid GetSelf() const override { return m_self; }
            Health GetHealth(Guid unit) const override
            {
                auto found = m_health.find(unit);
                return found == m_health.end() ? Health{0, 0} : found->second;
            }
            SpellId GetCurrentCast(Guid unit) const override
            {
                auto found = m_casts.find(unit);
                return found == m_casts.end() ? NoSpell : found->second;
            }

            // BotActions
            void CastSpell(SpellId spell, Guid target) override
            {
                std::ostringstream deed;
                deed << "cast " << spell << " on 0x" << std::hex << std::setw(16) << std::setfill('0') << target;
                m_deeds.push_back(deed.str());
            }
            void MoveTo(Position const& destination) override
            {
                std::ostringstream deed;
                deed << "move to " << destination.x << ' ' << destination.y << ' ' << destination.z;
                m_deeds.push_back(deed.str());
            }

        private:
            Guid m_self;
            std::map<Guid, Health> m_health;
            std::map<Guid, SpellId> m_casts;
            std::vector<std::string> m_deeds;
        };
    }
}

#endif
