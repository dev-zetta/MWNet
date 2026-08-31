#ifndef OPENMW_MP_MECHANICS_ACTOR_MAGIC_REGISTRY_HPP
#define OPENMW_MP_MECHANICS_ACTOR_MAGIC_REGISTRY_HPP

#include "InventoryLedger.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace mwmp::mechanics
{
    struct ActorMagicTemplate
    {
        std::string refId;
        double maximumHealth = 0;
        double maximumMagicka = 0;
        double maximumFatigue = 0;
        double willpower = 0;
        double luck = 0;
        double enchantSkill = 0;
        std::unordered_map<std::string, double> magicSkills;
        std::unordered_set<std::string> spells;
        std::vector<InventoryItem> inventory;

        bool operator==(const ActorMagicTemplate&) const = default;
    };

    class ActorMagicRegistry
    {
    public:
        static constexpr std::size_t MaximumActors = 65536;
        static constexpr std::size_t MaximumSpells = 4096;
        static constexpr std::size_t MaximumSkills = 64;
        static constexpr std::size_t MaximumInventoryStacks = 4096;
        static constexpr std::size_t MaximumStringBytes = 4096;
        static constexpr double MaximumStatValue = 1'000'000.0;

        explicit ActorMagicRegistry(std::size_t maximumActors = MaximumActors);

        bool upsert(ActorMagicTemplate actor);
        std::optional<ActorMagicTemplate> find(const std::string& refId) const;
        bool knowsSpell(const std::string& refId, const std::string& spellId) const;
        bool erase(const std::string& refId) noexcept;
        void clear() noexcept;
        std::size_t size() const noexcept;

    private:
        static bool valid(const ActorMagicTemplate& actor) noexcept;
        static bool validId(const std::string& value, bool allowEmpty = false) noexcept;

        std::size_t mMaximumActors;
        std::unordered_map<std::string, ActorMagicTemplate> mActors;
    };
}

#endif
