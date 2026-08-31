#ifndef OPENMW_MP_MECHANICS_COMBAT_RESOLVER_HPP
#define OPENMW_MP_MECHANICS_COMBAT_RESOLVER_HPP

#include "MovementValidator.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace mwmp::mechanics
{
    enum class CombatantKind : std::uint8_t
    {
        Player,
        Actor,
    };

    struct CombatantId
    {
        CombatantKind kind = CombatantKind::Player;
        std::uint64_t value = 0;
        // Actor reference numbers are cell-local. Player identities leave this
        // empty, while actor identities carry the canonical cell description.
        std::string scope;

        bool operator==(const CombatantId&) const = default;
    };

    struct CombatantIdHash
    {
        std::size_t operator()(const CombatantId& id) const noexcept;
    };

    struct CombatantState
    {
        double health = 0;
        double maximumHealth = 0;
        double fatigueRatio = 1;
        double accuracy = 0.5;
        double evasion = 0;
        double armorRating = 0;
        double minimumDamage = 0;
        double maximumDamage = 0;
        double meleeReach = 0;
        double projectileReach = 0;
        Position3 position;
        bool alive = true;
    };

    struct CombatantRelocation
    {
        CombatantId source;
        CombatantId destination;
        Position3 position;

        bool operator==(const CombatantRelocation&) const = default;
    };

    enum class AttackKind : std::uint8_t
    {
        Melee,
        Ranged,
    };

    struct AttackIntent
    {
        CombatantId attacker;
        CombatantId target;
        std::uint64_t sequence = 0;
        AttackKind kind = AttackKind::Melee;
        double strength = 0;
    };

    enum class CombatDecision : std::uint8_t
    {
        AppliedHit,
        AppliedMiss,
        InvalidEntity,
        InvalidSequence,
        StaleSequence,
        InvalidIntent,
        InvalidState,
        AttackerDead,
        TargetDead,
        OutOfRange,
        CapacityReached,
    };

    struct CombatResult
    {
        CombatDecision decision = CombatDecision::InvalidIntent;
        std::uint64_t sequence = 0;
        double hitChance = 0;
        double damage = 0;
        double targetHealth = 0;
        bool targetDied = false;

        bool applied() const noexcept
        {
            return decision == CombatDecision::AppliedHit
                || decision == CombatDecision::AppliedMiss;
        }
    };

    class CombatResolver
    {
    public:
        static constexpr double MinimumHitChance = 0.05;
        static constexpr double MaximumHitChance = 0.95;
        static constexpr double MinimumFatigueMultiplier = 0.5;
        static constexpr double MaximumFatigueMultiplier = 1.0;
        static constexpr double MaximumStatValue = 1'000'000.0;

        explicit CombatResolver(std::size_t maximumCombatants = 16384);

        bool upsert(CombatantId id, const CombatantState& state);
        CombatResult resolve(const AttackIntent& intent, double serverRoll);
        bool previewRelocations(
            const std::vector<CombatantRelocation>& relocations) const;
        bool applyRelocations(
            const std::vector<CombatantRelocation>& relocations);

        std::optional<CombatantState> find(CombatantId id) const;
        bool erase(CombatantId id) noexcept;
        void clear() noexcept;
        std::size_t size() const noexcept;

    private:
        static bool validId(const CombatantId& id) noexcept;
        static bool validState(const CombatantState& state) noexcept;
        static bool validPosition(const Position3& position) noexcept;
        static double distance(const Position3& left, const Position3& right) noexcept;

        std::size_t mMaximumCombatants;
        std::unordered_map<CombatantId, CombatantState, CombatantIdHash> mCombatants;
        std::unordered_map<CombatantId, std::uint64_t, CombatantIdHash> mSequences;
    };

    const char* describe(CombatDecision decision) noexcept;
}

#endif
