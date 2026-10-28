#include "GroupCoordinator.h"

#include <algorithm>

namespace Bots
{
    using Lock = std::lock_guard<std::mutex>;

    bool GroupCoordinator::PullsNow(Guid puller) const
    {
        return m_pull.phase != PullPhase::None && m_pull.puller == puller;
    }

    Guid GroupCoordinator::GetFocusTarget() const
    {
        Lock lock(m_mutex);
        return m_focusTarget;
    }

    void GroupCoordinator::SetFocusTarget(Guid target)
    {
        Lock lock(m_mutex);
        if (target != m_focusTarget)
        {
            m_tauntGivenUp = NoGuid;
            m_tankOnTop = false;
        }
        m_focusTarget = target;
    }

    PullState GroupCoordinator::GetPull() const
    {
        Lock lock(m_mutex);
        return m_pull;
    }

    void GroupCoordinator::StartPull(Guid puller, Guid target)
    {
        Lock lock(m_mutex);
        m_pull = {PullPhase::Pulling, puller, target};
        m_memberSpots.clear();
        ++m_memberSpotsVersion;
        m_focusTarget = target;
        m_tauntGivenUp = NoGuid;
    }

    void GroupCoordinator::HoldPull(Guid puller, std::int64_t nowMs)
    {
        Lock lock(m_mutex);
        if (m_pull.phase == PullPhase::Pulling && m_pull.puller == puller)
        {
            m_pull.phase = PullPhase::Holding;
            m_pull.engagedMs = nowMs;
            m_pull.backAtFightSpot = !m_pull.hasFightSpot;
        }
    }

    void GroupCoordinator::NoteFocusContact(std::int64_t nowMs)
    {
        Lock lock(m_mutex);
        if (m_pull.phase == PullPhase::Holding && !m_pull.hasContact)
        {
            m_pull.hasContact = true;
            m_pull.contactMs = nowMs;
        }
    }

    void GroupCoordinator::SetFightSpot(Guid puller, Position spot)
    {
        Lock lock(m_mutex);
        if (!PullsNow(puller))
            return;
        m_pull.hasFightSpot = true;
        m_pull.fightSpot = spot;
    }

    void GroupCoordinator::ClearFightSpot(Guid puller)
    {
        Lock lock(m_mutex);
        if (m_pull.puller != puller)
            return;
        m_pull.hasFightSpot = false;
        if (m_pull.phase == PullPhase::Holding)
            m_pull.backAtFightSpot = true;
    }

    void GroupCoordinator::SetShootSpot(Guid puller, Position spot)
    {
        Lock lock(m_mutex);
        if (!PullsNow(puller))
            return;
        m_pull.hasShootSpot = true;
        m_pull.shootSpot = spot;
    }

    void GroupCoordinator::ClearShootSpot(Guid puller)
    {
        Lock lock(m_mutex);
        if (m_pull.puller == puller)
            m_pull.hasShootSpot = false;
    }

    void GroupCoordinator::CountRangeChase(Guid puller, bool moving)
    {
        Lock lock(m_mutex);
        if (!PullsNow(puller))
            return;
        m_pull.stillChaseThinks = moving ? 0 : m_pull.stillChaseThinks + 1;
    }

    void GroupCoordinator::EndPull()
    {
        Lock lock(m_mutex);
        m_pull = {PullPhase::None, NoGuid, NoGuid};
        m_memberSpots.clear();
        ++m_memberSpotsVersion;
        m_focusTarget = NoGuid;
        m_tauntGivenUp = NoGuid;
        m_tankOnTop = false;
    }

    void GroupCoordinator::StartGroupWait(Guid puller, std::int64_t nowMs)
    {
        Lock lock(m_mutex);
        if (!PullsNow(puller))
            return;
        m_pull.waitFromMs = nowMs;
        m_pull.waitSaid = GroupWaitSaid::Nothing;
    }

    void GroupCoordinator::NoteGroupWaitSaid(Guid puller, GroupWaitSaid said)
    {
        Lock lock(m_mutex);
        if (PullsNow(puller))
            m_pull.waitSaid = said;
    }

    void GroupCoordinator::NoteExtraPackWarn(Guid puller)
    {
        Lock lock(m_mutex);
        if (PullsNow(puller))
            m_pull.extraPackWarned = true;
    }

    void GroupCoordinator::GoIn(Guid puller)
    {
        Lock lock(m_mutex);
        if (PullsNow(puller))
            m_pull.goesIn = true;
    }

    MemberSpot GroupCoordinator::ClaimMemberSpot(Guid bot, MemberSpotFinder const& find)
    {
        for (;;)
        {
            PullState pull;
            std::vector<Position> taken;
            std::uint64_t version = 0;
            {
                Lock lock(m_mutex);
                for (MemberSpot const& member : m_memberSpots)
                {
                    if (member.bot == bot)
                        return member;
                    if (member.found)
                        taken.push_back(member.spot);
                }
                pull = m_pull;
                version = m_memberSpotsVersion;
            }
            MemberSpot mine;
            mine.bot = bot;
            mine.found = find(pull, taken, mine.spot);
            Lock lock(m_mutex);
            if (version != m_memberSpotsVersion)
                continue;
            m_memberSpots.push_back(mine);
            ++m_memberSpotsVersion;
            return mine;
        }
    }

    void GroupCoordinator::SetMemberInPlace(Guid bot)
    {
        Lock lock(m_mutex);
        for (MemberSpot& member : m_memberSpots)
            if (member.bot == bot)
                member.inPlace = true;
    }

    std::vector<MemberSpot> GroupCoordinator::GetMemberSpots() const
    {
        Lock lock(m_mutex);
        return m_memberSpots;
    }

    void GroupCoordinator::NoteBot(Guid bot, std::int64_t nowMs, bool joinsPulls)
    {
        Lock lock(m_mutex);
        m_bots[bot] = {nowMs, joinsPulls};
    }

    std::vector<Guid> GroupCoordinator::GetPullMembers(std::int64_t nowMs, std::int64_t maxAgeMs) const
    {
        Lock lock(m_mutex);
        std::vector<Guid> bots;
        for (auto const& pair : m_bots)
            if (pair.second.joinsPulls && nowMs - pair.second.atMs <= maxAgeMs)
                bots.push_back(pair.first);
        return bots;
    }

    bool GroupCoordinator::TankHoldsThreat(std::int64_t nowMs) const
    {
        return TankHoldsThreatBy(nowMs) != ThreatHeld::No;
    }

    void GroupCoordinator::NoteTankOnTop(bool onTop, std::int64_t nowMs)
    {
        Lock lock(m_mutex);
        if (onTop && !m_tankOnTop)
            m_tankOnTopMs = nowMs;
        m_tankOnTop = onTop;
    }

    ThreatHeld GroupCoordinator::TankHoldsThreatBy(std::int64_t nowMs) const
    {
        Lock lock(m_mutex);
        if (m_playerTank)
            return m_focusTarget != NoGuid && m_tankOnTop && nowMs - m_tankOnTopMs >= PlayerTankHoldMs
                ? ThreatHeld::TopOfThreat
                : ThreatHeld::No;
        if (m_pull.phase == PullPhase::None)
            return m_focusTarget != NoGuid ? ThreatHeld::OutsideAPull : ThreatHeld::No;
        if (m_pull.phase != PullPhase::Holding)
            return ThreatHeld::No;
        if (!m_pull.backAtFightSpot)
            return ThreatHeld::No;
        if (m_pull.hasContact && nowMs - m_pull.contactMs >= HoldContactMs)
            return ThreatHeld::MeleeContact;
        if (nowMs - m_pull.engagedMs >= HoldLimitMs)
            return ThreatHeld::TimeLimit;
        return ThreatHeld::No;
    }

    void GroupCoordinator::GiveUpTaunt(Guid enemy)
    {
        Lock lock(m_mutex);
        m_tauntGivenUp = enemy;
    }

    bool GroupCoordinator::IsTauntGivenUp(Guid enemy) const
    {
        Lock lock(m_mutex);
        return enemy != NoGuid && m_tauntGivenUp == enemy;
    }

    bool GroupCoordinator::ClaimInterrupt(Guid caster, SpellId spell, Guid bot)
    {
        Lock lock(m_mutex);
        auto found = m_interruptClaims.find(caster);
        if (found != m_interruptClaims.end() && found->second.spell == spell && found->second.bot != bot)
            return false;
        m_interruptClaims[caster] = {caster, spell, bot};
        return true;
    }

    Guid GroupCoordinator::GetInterruptClaim(Guid caster, SpellId spell) const
    {
        Lock lock(m_mutex);
        auto found = m_interruptClaims.find(caster);
        return found != m_interruptClaims.end() && found->second.spell == spell ? found->second.bot : NoGuid;
    }

    std::vector<InterruptClaim> GroupCoordinator::GetInterruptClaims() const
    {
        Lock lock(m_mutex);
        std::vector<InterruptClaim> claims;
        for (auto const& pair : m_interruptClaims)
            claims.push_back(pair.second);
        return claims;
    }

    void GroupCoordinator::ReleaseInterrupt(Guid caster)
    {
        Lock lock(m_mutex);
        m_interruptClaims.erase(caster);
    }

    void GroupCoordinator::MarkEnemyHealer(Guid enemy)
    {
        Lock lock(m_mutex);
        if (!Contains(m_enemyHealers, enemy))
            m_enemyHealers.push_back(enemy);
    }

    void GroupCoordinator::ForgetEnemyHealer(Guid enemy)
    {
        Lock lock(m_mutex);
        m_enemyHealers.erase(std::remove(m_enemyHealers.begin(), m_enemyHealers.end(), enemy), m_enemyHealers.end());
    }

    std::vector<Guid> GroupCoordinator::GetEnemyHealers() const
    {
        Lock lock(m_mutex);
        return m_enemyHealers;
    }

    Guid GroupCoordinator::GetTank() const
    {
        Lock lock(m_mutex);
        return m_tank;
    }

    void GroupCoordinator::SetTank(Guid tank, bool playerTank)
    {
        Lock lock(m_mutex);
        m_tank = tank;
        m_playerTank = playerTank && tank != NoGuid;
    }

    bool GroupCoordinator::IsPlayerTankGroup() const
    {
        Lock lock(m_mutex);
        return m_playerTank;
    }

    Guid GroupCoordinator::GetHealer() const
    {
        Lock lock(m_mutex);
        return m_healer;
    }

    void GroupCoordinator::SetHealer(Guid healer)
    {
        Lock lock(m_mutex);
        m_healer = healer;
    }

    void GroupCoordinator::SetMemberRole(Guid member, BotRole role)
    {
        Lock lock(m_mutex);
        m_roles[member] = role;
    }

    bool GroupCoordinator::HasRole(Guid member, BotRole role) const
    {
        Lock lock(m_mutex);
        auto const found = m_roles.find(member);
        return found != m_roles.end() && found->second == role;
    }

    void GroupCoordinator::KeepTrail(Position at, bool fights)
    {
        Lock lock(m_mutex);
        // A pull keeps the trail as it was at its start: the tank's walk to
        // its shoot spot is no cleared way.
        if (fights)
        {
            // The first think of the fight: the trail becomes its cleared way.
            if (!m_trail.Points().empty())
                m_fightWay = m_trail.Points();
            m_trail.Clear();
            return;
        }
        m_fightWay.clear();
        if (m_pull.phase == PullPhase::None)
            m_trail.Note(at);
    }

    std::vector<Position> GroupCoordinator::GetFightWay() const
    {
        Lock lock(m_mutex);
        return m_fightWay;
    }

    std::vector<Position> GroupCoordinator::GetTrail() const
    {
        Lock lock(m_mutex);
        return m_trail.Points();
    }

    BossFight GroupCoordinator::GetBossFight() const
    {
        Lock lock(m_mutex);
        return m_bossFight;
    }

    bool GroupCoordinator::StartBossFight(Guid boss, std::uint32_t entry)
    {
        Lock lock(m_mutex);
        if (m_bossFight.boss != NoGuid)
            return m_bossFight.boss == boss;
        m_bossFight = {boss, entry};
        return true;
    }

    void GroupCoordinator::EndBossFight(Guid boss)
    {
        Lock lock(m_mutex);
        if (m_bossFight.boss == boss)
            m_bossFight = {NoGuid, 0};
    }

    BossFight GroupCoordinator::GetBossFought() const
    {
        Lock lock(m_mutex);
        return m_bossFought;
    }

    bool GroupCoordinator::ReplaceBossFought(BossFight expected, BossFight next)
    {
        Lock lock(m_mutex);
        if (m_bossFought.boss != expected.boss)
            return false;
        m_bossFought = next;
        return true;
    }

    bool GroupCoordinator::IsWiped() const
    {
        Lock lock(m_mutex);
        return m_wiped;
    }

    void GroupCoordinator::SetWiped(bool wiped)
    {
        Lock lock(m_mutex);
        if (wiped && !m_wiped)
        {
            GroupEvent event = {GroupEventKind::Wipe};
            event.entry = m_bossFought.entry;
            m_events.push_back(event);
        }
        m_wiped = wiped;
    }

    void GroupCoordinator::SetResting(Guid bot, RestReason reason)
    {
        Lock lock(m_mutex);
        if (reason == RestReason::None)
        {
            m_resting.erase(bot);
            return;
        }
        if (m_resting.emplace(bot, reason).second)
            m_events.push_back({GroupEventKind::Rest, bot, reason});
    }

    std::vector<Guid> GroupCoordinator::GetResting() const
    {
        Lock lock(m_mutex);
        std::vector<Guid> bots;
        for (auto const& pair : m_resting)
            bots.push_back(pair.first);
        for (Guid bot : m_restUntilFull)
            if (!m_resting.count(bot))
                bots.push_back(bot);
        return bots;
    }

    void GroupCoordinator::ForgetRest(Guid bot)
    {
        Lock lock(m_mutex);
        m_resting.erase(bot);
        m_restUntilFull.erase(bot);
    }

    void GroupCoordinator::SetRestUntilFull(Guid bot)
    {
        Lock lock(m_mutex);
        m_restUntilFull.insert(bot);
    }

    bool GroupCoordinator::RestsUntilFull(Guid bot) const
    {
        Lock lock(m_mutex);
        return m_restUntilFull.count(bot) != 0;
    }

    void GroupCoordinator::ClearRestUntilFull(Guid bot)
    {
        Lock lock(m_mutex);
        m_restUntilFull.erase(bot);
    }

    void GroupCoordinator::NoteReleaseToRunBack(Guid bot)
    {
        Lock lock(m_mutex);
        m_events.push_back({GroupEventKind::Rest, bot, RestReason::Revive});
    }

    void GroupCoordinator::GiveUpResurrect(Guid bot, std::vector<Guid> const& blockers)
    {
        Lock lock(m_mutex);
        m_givenUpResurrects[bot] = blockers;
    }

    bool GroupCoordinator::IsResurrectGivenUp(Guid bot) const
    {
        Lock lock(m_mutex);
        return m_givenUpResurrects.count(bot) != 0;
    }

    void GroupCoordinator::ClearGivenUpResurrect(Guid bot)
    {
        Lock lock(m_mutex);
        m_givenUpResurrects.erase(bot);
    }

    void GroupCoordinator::RetryResurrects(EnemyLives const& lives)
    {
        Lock lock(m_mutex);
        for (auto it = m_givenUpResurrects.begin(); it != m_givenUpResurrects.end();)
        {
            std::vector<Guid> const& blockers = it->second;
            if (std::any_of(blockers.begin(), blockers.end(), [&lives](Guid enemy) { return !lives(enemy); }))
                it = m_givenUpResurrects.erase(it);
            else
                ++it;
        }
    }

    bool GroupCoordinator::FirstNote(std::pair<Guid, std::int64_t> key, std::int64_t nowMs)
    {
        for (auto it = m_seen.begin(); it != m_seen.end();)
        {
            if (nowMs - it->second > GroupEventMemoryMs)
                it = m_seen.erase(it);
            else
                ++it;
        }
        return m_seen.emplace(key, nowMs).second;
    }

    void GroupCoordinator::NoteBossKill(Guid boss, std::uint32_t entry, std::int64_t nowMs)
    {
        Lock lock(m_mutex);
        // Key -1: no loot slot, so a kill never matches a roll.
        if (!FirstNote({boss, -1}, nowMs))
            return;
        GroupEvent event = {GroupEventKind::BossKill};
        event.target = boss;
        event.entry = entry;
        m_events.push_back(event);
    }

    void GroupCoordinator::NoteRollWon(Guid looted, std::uint32_t itemSlot, std::uint32_t itemEntry, Guid winner, std::int64_t nowMs)
    {
        Lock lock(m_mutex);
        if (!FirstNote({looted, itemSlot}, nowMs))
            return;
        GroupEvent event = {GroupEventKind::LootRoll};
        event.target = looted;
        event.entry = itemEntry;
        event.winner = winner;
        m_events.push_back(event);
    }

    std::vector<GroupEvent> GroupCoordinator::TakeEvents()
    {
        Lock lock(m_mutex);
        std::vector<GroupEvent> events;
        events.swap(m_events);
        return events;
    }

    // jvbastel/WOWAI#170
    bool GroupCoordinator::NoteHealthFalls(Guid bot, std::uint32_t health, std::int64_t nowMs)
    {
        Lock lock(m_mutex);
        std::vector<HealthNote>& notes = m_healthNotes[bot];
        // A note from the future (the clock went back) is dropped too.
        notes.erase(std::remove_if(notes.begin(), notes.end(),
                        [nowMs](HealthNote const& note) {
                            return nowMs - note.atMs > HealthFallMs || note.atMs > nowMs;
                        }),
            notes.end());
        bool const falls = std::any_of(notes.begin(), notes.end(),
            [health](HealthNote const& note) { return note.health > health; });
        notes.push_back(HealthNote{nowMs, health});
        return falls;
    }
}
