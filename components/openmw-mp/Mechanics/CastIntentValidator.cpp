#include "CastIntentValidator.hpp"

#include <algorithm>
#include <cmath>

namespace mwmp::mechanics
{
    CastIntentDecision CastIntentValidator::validate(const CastIntent& intent) const noexcept
    {
        if (!validCombatant(intent.caster))
            return CastIntentDecision::InvalidCaster;
        if (intent.target)
        {
            if (!validCombatant(*intent.target))
                return CastIntentDecision::InvalidTarget;
            if (intent.target->kind == CombatantKind::Actor
                && intent.caster.kind == CombatantKind::Actor
                && intent.target->scope != intent.caster.scope)
            {
                return CastIntentDecision::InvalidTarget;
            }
        }
        if (!validSourceId(intent.sourceId))
            return CastIntentDecision::InvalidSource;
        if (intent.projectile && !validProjectile(*intent.projectile))
            return CastIntentDecision::InvalidProjectile;
        return CastIntentDecision::Accepted;
    }

    bool CastIntentValidator::validCombatant(const CombatantId& combatant) noexcept
    {
        if (combatant.value == 0)
            return false;
        if (combatant.kind == CombatantKind::Player)
            return combatant.scope.empty();
        return !combatant.scope.empty();
    }

    bool CastIntentValidator::validSourceId(const std::string& sourceId) noexcept
    {
        if (sourceId.empty() || sourceId.size() > MaximumSourceIdBytes)
            return false;
        return std::ranges::none_of(sourceId, [](unsigned char value) {
            return value == 0 || value < 0x20 || value == 0x7f;
        });
    }

    bool CastIntentValidator::validProjectile(const ProjectileIntent& projectile) noexcept
    {
        const auto validCoordinate = [](double value) {
            return std::isfinite(value) && std::abs(value) <= MaximumCoordinate;
        };
        if (!validCoordinate(projectile.origin.x)
            || !validCoordinate(projectile.origin.y)
            || !validCoordinate(projectile.origin.z)
            || !std::ranges::all_of(projectile.orientation, [](double value) {
                return std::isfinite(value) && std::abs(value) <= 2.0;
            }))
        {
            return false;
        }

        double normSquared = 0;
        for (double value : projectile.orientation)
            normSquared += value * value;
        return normSquared >= 0.25 && normSquared <= 4.0;
    }

    const char* describe(CastIntentDecision decision) noexcept
    {
        switch (decision)
        {
            case CastIntentDecision::Accepted:
                return "the cast intent was accepted";
            case CastIntentDecision::InvalidCaster:
                return "the cast intent has an invalid caster";
            case CastIntentDecision::InvalidTarget:
                return "the cast intent has an invalid target";
            case CastIntentDecision::InvalidSource:
                return "the cast intent has an invalid spell or item ID";
            case CastIntentDecision::InvalidProjectile:
                return "the cast intent has invalid projectile geometry";
        }
        return "unknown cast-intent decision";
    }
}
