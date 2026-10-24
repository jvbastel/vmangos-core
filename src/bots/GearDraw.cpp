#include "GearDraw.h"

#include <algorithm>

namespace Bots
{
    namespace
    {
        // SplitMix64: a small random generator with the same numbers on every
        // compiler and platform, unlike the distributions of <random>.
        class Random
        {
            public:
                explicit Random(std::uint64_t seed) : m_state(seed) {}

                std::uint64_t Next()
                {
                    std::uint64_t z = (m_state += 0x9E3779B97F4A7C15ULL);
                    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
                    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
                    return z ^ (z >> 31);
                }

                // A number from 0 to count - 1.
                std::uint64_t Below(std::uint64_t count) { return Next() % count; }

            private:
                std::uint64_t m_state;
        };

        // FNV-1a of a word, so that each stream has its own seed.
        std::uint64_t Hash(std::string const& word)
        {
            std::uint64_t hash = 0xCBF29CE484222325ULL;
            for (char c : word)
            {
                hash ^= static_cast<unsigned char>(c);
                hash *= 0x100000001B3ULL;
            }
            return hash;
        }

        Random StreamFor(std::uint64_t guid, std::string const& stream)
        {
            Random mix(guid ^ Hash(stream));
            return Random(mix.Next());
        }

        bool Allowed(GearItem const& item, std::uint32_t level, std::uint32_t patch)
        {
            return item.minLevel <= level && level <= item.maxLevel && item.minPatch <= patch;
        }

        // The items of a slot for the level and patch, best score first, one
        // line per entry.
        std::vector<GearItem> Pool(GearTemplate const& gear, GearSlot slot, std::uint32_t level, std::uint32_t patch)
        {
            std::vector<GearItem> pool;
            for (GearItem const& item : gear.items)
            {
                if (item.slot != slot || !Allowed(item, level, patch))
                    continue;
                bool known = false;
                for (GearItem& other : pool)
                    if (other.entry == item.entry)
                    {
                        known = true;
                        other.score = std::max(other.score, item.score);
                    }
                if (!known)
                    pool.push_back(item);
            }
            std::sort(pool.begin(), pool.end(), [](GearItem const& a, GearItem const& b)
            {
                return a.score != b.score ? a.score > b.score : a.entry < b.entry;
            });
            if (pool.size() > GearPoolSize)
                pool.resize(GearPoolSize);
            return pool;
        }

        // Takes one item out of the pool: rank r of n weighs n - r.
        GearItem TakeWeighted(std::vector<GearItem>& pool, Random& random)
        {
            std::uint64_t const n = pool.size();
            std::uint64_t ticket = random.Below(n * (n + 1) / 2);
            std::size_t rank = 0;
            while (ticket >= n - rank)
            {
                ticket -= n - rank;
                ++rank;
            }
            GearItem const item = pool[rank];
            pool.erase(pool.begin() + rank);
            return item;
        }

        // The positions that a slot fills, in order. A two-handed weapon
        // stands in the main hand and keeps the off hand empty.
        std::vector<GearPosition> PositionsOf(GearSlot slot)
        {
            switch (slot)
            {
                case GearSlot::Head: return {GearPosition::Head};
                case GearSlot::Neck: return {GearPosition::Neck};
                case GearSlot::Shoulders: return {GearPosition::Shoulders};
                case GearSlot::Back: return {GearPosition::Back};
                case GearSlot::Chest: return {GearPosition::Chest};
                case GearSlot::Shirt: return {GearPosition::Shirt};
                case GearSlot::Tabard: return {GearPosition::Tabard};
                case GearSlot::Wrists: return {GearPosition::Wrists};
                case GearSlot::Hands: return {GearPosition::Hands};
                case GearSlot::Waist: return {GearPosition::Waist};
                case GearSlot::Legs: return {GearPosition::Legs};
                case GearSlot::Feet: return {GearPosition::Feet};
                case GearSlot::Finger: return {GearPosition::Finger1, GearPosition::Finger2};
                case GearSlot::Trinket: return {GearPosition::Trinket1, GearPosition::Trinket2};
                case GearSlot::Ranged: return {GearPosition::Ranged};
                case GearSlot::MainHand:
                case GearSlot::TwoHand: return {GearPosition::MainHand};
                case GearSlot::OffHand:
                case GearSlot::Shield:
                case GearSlot::Holdable: break;
            }
            return {GearPosition::OffHand};
        }

        bool IsWeaponSlot(GearSlot slot)
        {
            return slot == GearSlot::MainHand || slot == GearSlot::OffHand || slot == GearSlot::TwoHand ||
                slot == GearSlot::Shield || slot == GearSlot::Holdable;
        }

        GearSlot const ArmorSlots[] = {
            GearSlot::Head, GearSlot::Neck, GearSlot::Shoulders, GearSlot::Back, GearSlot::Chest,
            GearSlot::Shirt, GearSlot::Tabard, GearSlot::Wrists, GearSlot::Hands, GearSlot::Waist,
            GearSlot::Legs, GearSlot::Feet, GearSlot::Finger, GearSlot::Trinket, GearSlot::Ranged,
        };

        void DrawSlot(GearTemplate const& gear, GearSlot slot, std::uint64_t guid, std::uint32_t level,
            std::uint32_t patch, std::vector<GearPick>& picks)
        {
            std::vector<GearItem> pool = Pool(gear, slot, level, patch);
            Random random = StreamFor(guid, GearSlotName(slot));
            for (GearPosition position : PositionsOf(slot))
            {
                if (pool.empty())
                    break;
                picks.push_back({position, slot, TakeWeighted(pool, random).entry});
            }
        }

        WeaponShape const* ChooseShape(GearTemplate const& gear, std::uint64_t guid, std::uint32_t level, std::uint32_t patch)
        {
            std::vector<WeaponShape const*> full;
            WeaponShape const* fullest = nullptr;
            std::size_t fullestCount = 0;
            for (WeaponShape const& shape : gear.shapes)
            {
                std::size_t filled = 0;
                for (GearSlot slot : shape.slots)
                    filled += Pool(gear, slot, level, patch).empty() ? 0 : 1;
                if (filled == shape.slots.size())
                    full.push_back(&shape);
                if (filled > fullestCount)
                {
                    fullest = &shape;
                    fullestCount = filled;
                }
            }
            if (full.empty())
                return fullest;
            Random random = StreamFor(guid, "shape");
            return full[random.Below(full.size())];
        }

        void SortByPosition(std::vector<GearPick>& picks)
        {
            std::stable_sort(picks.begin(), picks.end(), [](GearPick const& a, GearPick const& b)
            {
                return a.position < b.position;
            });
        }

        // Each allowed item, the later content patch first. An item that
        // finds one of its positions taken is left out.
        GearDraw WearHandMade(GearTemplate const& gear, std::uint32_t level, std::uint32_t patch)
        {
            std::vector<GearItem> allowed;
            for (GearItem const& item : gear.items)
                if (Allowed(item, level, patch))
                    allowed.push_back(item);
            std::stable_sort(allowed.begin(), allowed.end(), [](GearItem const& a, GearItem const& b)
            {
                return a.minPatch > b.minPatch;
            });

            GearDraw draw;
            std::vector<GearPosition> taken;
            auto isTaken = [&taken](GearPosition position)
            {
                return std::find(taken.begin(), taken.end(), position) != taken.end();
            };
            for (GearItem const& item : allowed)
            {
                // The positions that the item needs: a two-handed weapon also
                // keeps the off hand empty.
                std::vector<GearPosition> needs = PositionsOf(item.slot);
                if (item.slot == GearSlot::TwoHand)
                    needs.push_back(GearPosition::OffHand);
                GearPosition wear = needs[0];
                if (needs.size() == 2 && item.slot != GearSlot::TwoHand)
                {
                    // Finger or trinket: the first free one of the pair.
                    if (isTaken(needs[0]))
                        wear = needs[1];
                    needs = {wear};
                }
                bool free = true;
                for (GearPosition position : needs)
                    free = free && !isTaken(position);
                if (!free)
                    continue;
                taken.insert(taken.end(), needs.begin(), needs.end());
                draw.picks.push_back({wear, item.slot, item.entry});
            }
            SortByPosition(draw.picks);
            return draw;
        }
    }

    GearDraw DrawGear(GearTemplate const& gearTemplate, std::uint64_t guid, std::uint32_t level, std::uint32_t patch)
    {
        if (gearTemplate.kind == GearTemplateKind::Hand)
            return WearHandMade(gearTemplate, level, patch);

        GearDraw draw;
        for (GearSlot slot : ArmorSlots)
            DrawSlot(gearTemplate, slot, guid, level, patch, draw.picks);
        if (WeaponShape const* shape = ChooseShape(gearTemplate, guid, level, patch))
        {
            draw.shape = shape->name;
            for (GearSlot slot : shape->slots)
                if (IsWeaponSlot(slot))
                    DrawSlot(gearTemplate, slot, guid, level, patch, draw.picks);
        }
        SortByPosition(draw.picks);
        return draw;
    }
}
