#ifndef OPENMW_MP_MECHANICS_CAST_INTENT_VALIDATOR_HPP
#define OPENMW_MP_MECHANICS_CAST_INTENT_VALIDATOR_HPP

#include "CombatResolver.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

namespace mwmp::mechanics
{
    enum class CastKind : std::uint8_t
    {
        Regular,
        Item,
    };

    struct ProjectileIntent
    {
        Position3 origin;
        std::array<double, 4> orientation{};

        bool operator==(const ProjectileIntent&) const = default;
    };

    struct TransformIntent
    {
        Position3 translation;
        Position3 rotation;

        bool operator==(const TransformIntent&) const = default;
    };

    struct CastIntent
    {
        CombatantId caster;
        std::optional<CombatantId> target;
        CastKind kind = CastKind::Regular;
        std::string sourceId;
        bool pressed = false;
        bool instant = false;
        std::optional<ProjectileIntent> projectile;
        std::optional<TransformIntent> reportedCasterTransform;
        std::optional<TransformIntent> canonicalCasterTransform;
        std::optional<TransformIntent> reportedCasterDirection;

        bool operator==(const CastIntent&) const = default;
    };

    enum class CastIntentDecision : std::uint8_t
    {
        Accepted,
        InvalidCaster,
        InvalidTarget,
        InvalidSource,
        InvalidProjectile,
        InvalidCasterTransform,
    };

    class CastIntentValidator
    {
    public:
        static constexpr std::size_t MaximumSourceIdBytes = 4096;
        static constexpr double MaximumCoordinate = 1'000'000'000.0;
        static constexpr double MaximumCasterDrift = 256.0;

        CastIntentDecision validate(const CastIntent& intent) const noexcept;

    private:
        static bool validCombatant(const CombatantId& combatant) noexcept;
        static bool validSourceId(const std::string& sourceId) noexcept;
        static bool validProjectile(const ProjectileIntent& projectile) noexcept;
        static bool validTransform(const TransformIntent& transform) noexcept;
        static double distance(const Position3& left, const Position3& right) noexcept;
    };

    const char* describe(CastIntentDecision decision) noexcept;
}

#endif
