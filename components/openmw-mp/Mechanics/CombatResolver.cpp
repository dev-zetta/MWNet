#include "CombatResolver.hpp"

#include <algorithm>
#include <cmath>
#include <functional>

namespace mwmp::mechanics
{
    std::size_t CombatantIdHash::operator()(const CombatantId& id) const noexcept
    {
        const std::size_t kind = static_cast<std::size_t>(id.kind);
        return std::hash<std::uint64_t>{}(id.value) ^ (kind << 1);
    }

    CombatResolver::CombatResolver(std::size_t maximumCombatants)
        : mMaximumCombatants(maximumCombatants)
    {
    }

    bool CombatResolver::upsert(CombatantId id, const CombatantState& state)
    {
        if (!validId(id) || !validState(state))
            return false;
        if (!mCombatants.contains(id) && mCombatants.size() >= mMaximumCombatants)
            return false;
        mCombatants.insert_or_assign(id, state);
        return true;
    }

    CombatResult CombatResolver::resolve(const AttackIntent& intent, double serverRoll)
    {
        CombatResult result;
        result.sequence = intent.sequence;

        if (!validId(intent.attacker) || !validId(intent.target)
            || intent.attacker == intent.target)
        {
            result.decision = CombatDecision::InvalidEntity;
            return result;
        }
        if (intent.sequence == 0)
        {
            result.decision = CombatDecision::InvalidSequence;
            return result;
        }

        const auto sequence = mSequences.find(intent.attacker);
        if (sequence != mSequences.end() && intent.sequence <= sequence->second)
        {
            result.decision = CombatDecision::StaleSequence;
            return result;
        }
        if (!std::isfinite(intent.strength) || intent.strength < 0 || intent.strength > 1
            || !std::isfinite(serverRoll) || serverRoll < 0 || serverRoll >= 1)
        {
            result.decision = CombatDecision::InvalidIntent;
            return result;
        }

        const auto attacker = mCombatants.find(intent.attacker);
        const auto target = mCombatants.find(intent.target);
        if (attacker == mCombatants.end() || target == mCombatants.end())
        {
            result.decision = CombatDecision::InvalidEntity;
            return result;
        }
        if (!validState(attacker->second) || !validState(target->second))
        {
            result.decision = CombatDecision::InvalidState;
            return result;
        }
        if (!attacker->second.alive || attacker->second.health <= 0)
        {
            result.decision = CombatDecision::AttackerDead;
            return result;
        }
        if (!target->second.alive || target->second.health <= 0)
        {
            result.decision = CombatDecision::TargetDead;
            return result;
        }

        mSequences.insert_or_assign(intent.attacker, intent.sequence);
        const double reach = intent.kind == AttackKind::Melee
            ? attacker->second.meleeReach : attacker->second.projectileReach;
        if (distance(attacker->second.position, target->second.position) > reach)
        {
            result.decision = CombatDecision::OutOfRange;
            result.targetHealth = target->second.health;
            return result;
        }

        result.hitChance = std::clamp(
            attacker->second.accuracy - target->second.evasion,
            MinimumHitChance, MaximumHitChance);
        result.targetHealth = target->second.health;
        if (serverRoll >= result.hitChance)
        {
            result.decision = CombatDecision::AppliedMiss;
            return result;
        }

        const double baseDamage = attacker->second.minimumDamage
            + (attacker->second.maximumDamage - attacker->second.minimumDamage)
                * intent.strength;
        const double fatigueMultiplier = MinimumFatigueMultiplier
            + (MaximumFatigueMultiplier - MinimumFatigueMultiplier)
                * attacker->second.fatigueRatio;
        const double armorMultiplier = 100.0 / (100.0 + target->second.armorRating);
        result.damage = std::min(target->second.health,
            std::max(0.0, baseDamage * fatigueMultiplier * armorMultiplier));

        CombatantState& canonicalTarget = target->second;
        canonicalTarget.health -= result.damage;
        canonicalTarget.alive = canonicalTarget.health > 0;
        result.targetHealth = canonicalTarget.health;
        result.targetDied = !canonicalTarget.alive;
        result.decision = CombatDecision::AppliedHit;
        return result;
    }

    std::optional<CombatantState> CombatResolver::find(CombatantId id) const
    {
        const auto found = mCombatants.find(id);
        if (found == mCombatants.end())
            return std::nullopt;
        return found->second;
    }

    bool CombatResolver::erase(CombatantId id) noexcept
    {
        mSequences.erase(id);
        return mCombatants.erase(id) != 0;
    }

    void CombatResolver::clear() noexcept
    {
        mSequences.clear();
        mCombatants.clear();
    }

    std::size_t CombatResolver::size() const noexcept
    {
        return mCombatants.size();
    }

    bool CombatResolver::validId(CombatantId id) noexcept
    {
        return id.value != 0;
    }

    bool CombatResolver::validState(const CombatantState& state) noexcept
    {
        const auto finiteRange = [](double value) {
            return std::isfinite(value) && value >= 0 && value <= MaximumStatValue;
        };
        return finiteRange(state.health) && finiteRange(state.maximumHealth)
            && state.health <= state.maximumHealth
            && std::isfinite(state.fatigueRatio) && state.fatigueRatio >= 0
            && state.fatigueRatio <= 1
            && std::isfinite(state.accuracy) && std::isfinite(state.evasion)
            && state.accuracy >= -MaximumStatValue && state.accuracy <= MaximumStatValue
            && state.evasion >= -MaximumStatValue && state.evasion <= MaximumStatValue
            && finiteRange(state.armorRating) && finiteRange(state.minimumDamage)
            && finiteRange(state.maximumDamage)
            && state.minimumDamage <= state.maximumDamage
            && finiteRange(state.meleeReach) && finiteRange(state.projectileReach)
            && validPosition(state.position)
            && (state.alive == (state.health > 0));
    }

    bool CombatResolver::validPosition(const Position3& position) noexcept
    {
        const auto valid = [](double value) {
            return std::isfinite(value)
                && std::abs(value) <= MovementValidator::MaximumCoordinateMagnitude;
        };
        return valid(position.x) && valid(position.y) && valid(position.z);
    }

    double CombatResolver::distance(const Position3& left, const Position3& right) noexcept
    {
        return std::hypot(left.x - right.x, left.y - right.y, left.z - right.z);
    }

    const char* describe(CombatDecision decision) noexcept
    {
        switch (decision)
        {
            case CombatDecision::AppliedHit:
                return "the server applied the hit";
            case CombatDecision::AppliedMiss:
                return "the server applied the miss";
            case CombatDecision::InvalidEntity:
                return "the attacker or target is invalid";
            case CombatDecision::InvalidSequence:
                return "the combat sequence is invalid";
            case CombatDecision::StaleSequence:
                return "the combat sequence is stale";
            case CombatDecision::InvalidIntent:
                return "the attack intent is invalid";
            case CombatDecision::InvalidState:
                return "the canonical combatant state is invalid";
            case CombatDecision::AttackerDead:
                return "the attacker is dead";
            case CombatDecision::TargetDead:
                return "the target is dead";
            case CombatDecision::OutOfRange:
                return "the target is outside the server-approved range";
            case CombatDecision::CapacityReached:
                return "the canonical combatant store is at capacity";
        }
        return "unknown combat decision";
    }
}
