#ifndef OPENMW_MP_MECHANICS_SPELL_RESOLVER_HPP
#define OPENMW_MP_MECHANICS_SPELL_RESOLVER_HPP

#include "ActiveEffectLedger.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
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
        Instant,
        Timed,
        DamageHealth,
        RestoreHealth,
    };

    enum class SpellSourceKind : std::uint8_t
    {
        Regular,
        Item,
    };

    struct SpellEffectDefinition
    {
        SpellEffectDefinition() = default;

        SpellEffectDefinition(std::string id, std::string effectArgument,
            SpellEffectKind effectKind, SpellRange effectRange,
            double minimum, double maximum, double effectDuration,
            double rangeLimit, std::string school = {},
            double difficulty = 0)
            : effectId(std::move(id))
            , argument(std::move(effectArgument))
            , kind(effectKind)
            , range(effectRange)
            , minimumMagnitude(minimum)
            , maximumMagnitude(maximum)
            , duration(effectDuration)
            , maximumRange(rangeLimit)
            , castingSchool(std::move(school))
            , castingDifficulty(difficulty)
        {
        }

        std::string effectId;
        std::string argument;
        SpellEffectKind kind = SpellEffectKind::Instant;
        SpellRange range = SpellRange::Self;
        double minimumMagnitude = 0;
        double maximumMagnitude = 0;
        double duration = 0;
        double maximumRange = 0;
        std::string castingSchool;
        double castingDifficulty = 0;

        bool operator==(const SpellEffectDefinition&) const = default;
    };

    struct SpellDefinition
    {
        std::string id;
        std::string displayName;
        SpellSourceKind sourceKind = SpellSourceKind::Regular;
        double magickaCost = 0;
        double itemChargeCost = 0;
        double itemMaximumCharge = 0;
        double baseSuccessChance = 1;
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
        double willpower = 0;
        double luck = 0;
        double fatigueTerm = 1;
        double soundMagnitude = 0;
        double enchantSkill = 10;
        bool silenced = false;
        std::unordered_map<std::string, double> magicSkills;

        bool operator==(const SpellCombatantState&) const = default;
    };

    struct SpellCastIntent
    {
        SpellCastIntent(CombatantId casterId,
            std::optional<CombatantId> targetId, std::string source,
            std::uint64_t castSequence,
            std::optional<double> itemCharge = std::nullopt)
            : caster(std::move(casterId))
            , target(std::move(targetId))
            , sourceId(std::move(source))
            , sequence(castSequence)
            , availableItemCharge(itemCharge)
        {
        }

        CombatantId caster;
        std::optional<CombatantId> target;
        std::string sourceId;
        std::uint64_t sequence = 0;
        std::optional<double> availableItemCharge;

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
        InsufficientItemCharge,
        CapacityReached,
    };

    struct SpellApplication
    {
        CombatantId target;
        double health = 0;
        bool died = false;
        std::optional<CanonicalActiveSpell> activeSpell;
    };

    struct SpellResult
    {
        SpellDecision decision = SpellDecision::InvalidIntent;
        std::uint64_t sequence = 0;
        double successChance = 0;
        double magickaSpent = 0;
        double itemChargeSpent = 0;
        std::string effectiveSchool;
        double targetHealth = 0;
        bool targetDied = false;
        std::optional<CanonicalActiveSpell> activeSpell;
        std::vector<SpellApplication> applications;

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
        void swap(SpellResolver& other) noexcept;
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
