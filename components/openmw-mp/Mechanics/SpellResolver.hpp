#ifndef OPENMW_MP_MECHANICS_SPELL_RESOLVER_HPP
#define OPENMW_MP_MECHANICS_SPELL_RESOLVER_HPP

#include "ActiveEffectLedger.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace mwmp::mechanics
{
    enum class SpellRange : std::uint8_t
    {
        Self,
        Touch,
        Target,
    };

    enum class SpellEffectKind : std::uint8_t
    {
        Timed,
        DamageHealth,
        RestoreHealth,
    };

    struct SpellEffectDefinition
    {
        std::string effectId;
        std::string argument;
        SpellEffectKind kind = SpellEffectKind::Timed;
        double minimumMagnitude = 0;
        double maximumMagnitude = 0;
        double duration = 0;

        bool operator==(const SpellEffectDefinition&) const = default;
    };

    struct SpellDefinition
    {
        std::string id;
        std::string displayName;
        SpellRange range = SpellRange::Self;
        double magickaCost = 0;
        double baseSuccessChance = 1;
        double maximumRange = 0;
        bool alwaysSucceeds = false;
        bool stacking = false;
        std::vector<SpellEffectDefinition> effects;

        bool operator==(const SpellDefinition&) const = default;
    };

    struct SpellCombatantState
    {
        double health = 0;
        double maximumHealth = 0;
        double magicka = 0;
        double maximumMagicka = 0;
        double castingMultiplier = 1;
        double resistance = 0;
        Position3 position;
        bool alive = true;

        bool operator==(const SpellCombatantState&) const = default;
    };

    struct SpellCastIntent
    {
        CombatantId caster;
        std::optional<CombatantId> target;
        std::string sourceId;
        std::uint64_t sequence = 0;

        bool operator==(const SpellCastIntent&) const = default;
    };

    enum class SpellDecision : std::uint8_t
    {
        Applied,
        Failed,
        InvalidEntity,
        InvalidSequence,
        StaleSequence,
        InvalidIntent,
        InvalidState,
        UnknownSpell,
        CasterDead,
        TargetDead,
        MissingTarget,
        UnexpectedTarget,
        OutOfRange,
        InsufficientMagicka,
        CapacityReached,
    };

    struct SpellResult
    {
        SpellDecision decision = SpellDecision::InvalidIntent;
        std::uint64_t sequence = 0;
        double successChance = 0;
        double magickaSpent = 0;
        double targetHealth = 0;
        bool targetDied = false;
        std::optional<CanonicalActiveSpell> activeSpell;

        bool resolved() const noexcept
        {
            return decision == SpellDecision::Applied
                || decision == SpellDecision::Failed;
        }

        bool applied() const noexcept
        {
            return decision == SpellDecision::Applied;
        }
    };

    class SpellResolver
    {
    public:
        static constexpr std::size_t MaximumCombatants = 16384;
        static constexpr std::size_t MaximumDefinitions = 65536;
        static constexpr std::size_t MaximumEffectsPerSpell = 256;
        static constexpr std::size_t MaximumStringBytes = 4096;
        static constexpr double MaximumStatValue = 1'000'000.0;
        static constexpr double MaximumDurationSeconds = 31'536'000.0;
        static constexpr double MaximumTargetRange = 1'000'000.0;

        explicit SpellResolver(std::size_t maximumCombatants = MaximumCombatants,
            std::size_t maximumDefinitions = MaximumDefinitions);

        bool upsertCombatant(CombatantId id, const SpellCombatantState& state);
        bool upsertDefinition(SpellDefinition definition);
        SpellResult resolve(const SpellCastIntent& intent, double successRoll,
            double magnitudeRoll);

        std::optional<SpellCombatantState> findCombatant(CombatantId id) const;
        std::optional<SpellDefinition> findDefinition(const std::string& id) const;
        bool eraseCombatant(CombatantId id) noexcept;
        bool eraseDefinition(const std::string& id) noexcept;
        void clear() noexcept;

    private:
        static bool validId(const CombatantId& id) noexcept;
        static bool validState(const SpellCombatantState& state) noexcept;
        static bool validDefinition(const SpellDefinition& definition) noexcept;
        static bool validString(const std::string& value) noexcept;
        static bool validRoll(double value) noexcept;
        static double distance(const Position3& left, const Position3& right) noexcept;

        std::size_t mMaximumCombatants;
        std::size_t mMaximumDefinitions;
        std::unordered_map<CombatantId, SpellCombatantState, CombatantIdHash>
            mCombatants;
        std::unordered_map<std::string, SpellDefinition> mDefinitions;
        std::unordered_map<CombatantId, std::uint64_t, CombatantIdHash> mSequences;
    };

    const char* describe(SpellDecision decision) noexcept;
}

#endif
