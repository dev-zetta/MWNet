#ifndef OPENMW_MP_MECHANICS_ACTIVE_EFFECT_LEDGER_HPP
#define OPENMW_MP_MECHANICS_ACTIVE_EFFECT_LEDGER_HPP

#include "CombatResolver.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace mwmp::mechanics
{
    struct CanonicalEffect
    {
        std::string effectId;
        std::string argument;
        double magnitude = 0;
        double duration = 0;
        double timeLeft = 0;

        bool operator==(const CanonicalEffect&) const = default;
    };

    struct CanonicalActiveSpell
    {
        std::string id;
        std::string displayName;
        bool stacking = false;
        std::int32_t timestampDay = 0;
        double timestampHour = 0;
        std::optional<CombatantId> caster;
        std::vector<CanonicalEffect> effects;

        bool operator==(const CanonicalActiveSpell&) const = default;
    };

    enum class ActiveEffectAction : std::uint8_t
    {
        Set,
        Add,
        Remove,
    };

    enum class ActiveEffectDecision : std::uint8_t
    {
        Applied,
        InvalidOwner,
        InvalidAction,
        InvalidSpell,
        MissingSpell,
        SpellLimitReached,
        OwnerLimitReached,
    };

    struct ActiveEffectResult
    {
        ActiveEffectDecision decision = ActiveEffectDecision::InvalidAction;
        std::size_t spellCount = 0;

        bool applied() const noexcept
        {
            return decision == ActiveEffectDecision::Applied;
        }
    };

    struct ActiveEffectOperation
    {
        CombatantId owner;
        ActiveEffectAction action = ActiveEffectAction::Set;
        std::vector<CanonicalActiveSpell> spells;

        bool operator==(const ActiveEffectOperation&) const = default;
    };

    class ActiveEffectLedger
    {
    public:
        static constexpr std::size_t MaximumActiveSpells = 4096;
        static constexpr std::size_t MaximumEffectsPerSpell = 256;
        static constexpr std::size_t MaximumStringBytes = 4096;
        static constexpr double MaximumMagnitude = 1'000'000.0;
        static constexpr double MaximumDurationSeconds = 31'536'000.0;

        explicit ActiveEffectLedger(std::size_t maximumOwners = 16384);

        ActiveEffectResult preview(CombatantId owner, ActiveEffectAction action,
            const std::vector<CanonicalActiveSpell>& spells) const;
        ActiveEffectResult apply(CombatantId owner, ActiveEffectAction action,
            const std::vector<CanonicalActiveSpell>& spells);
        ActiveEffectResult previewBatch(
            const std::vector<ActiveEffectOperation>& operations) const;
        ActiveEffectResult applyBatch(
            const std::vector<ActiveEffectOperation>& operations);

        std::optional<std::vector<CanonicalActiveSpell>> snapshot(CombatantId owner) const;
        bool erase(CombatantId owner) noexcept;
        void clear() noexcept;
        std::size_t size() const noexcept;

    private:
        static bool validOwner(const CombatantId& owner) noexcept;
        static bool validSpell(const CanonicalActiveSpell& spell) noexcept;
        static ActiveEffectResult applyTo(std::vector<CanonicalActiveSpell>& active,
            ActiveEffectAction action, const std::vector<CanonicalActiveSpell>& spells);
        static ActiveEffectResult addTo(std::vector<CanonicalActiveSpell>& active,
            const std::vector<CanonicalActiveSpell>& spells);
        static ActiveEffectResult removeFrom(std::vector<CanonicalActiveSpell>& active,
            const std::vector<CanonicalActiveSpell>& spells);
        ActiveEffectResult prepareBatch(
            const std::vector<ActiveEffectOperation>& operations,
            std::unordered_map<CombatantId, std::vector<CanonicalActiveSpell>,
                CombatantIdHash>& candidates) const;

        std::size_t mMaximumOwners;
        std::unordered_map<CombatantId, std::vector<CanonicalActiveSpell>, CombatantIdHash>
            mActiveEffects;
    };

    const char* describe(ActiveEffectDecision decision) noexcept;
}

#endif
