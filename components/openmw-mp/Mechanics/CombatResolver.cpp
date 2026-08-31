#include "CombatResolver.hpp"

#include <algorithm>
#include <cmath>
#include <functional>
#include <unordered_set>

namespace mwmp::mechanics
{
    std::size_t CombatantIdHash::operator()(const CombatantId& id) const noexcept
    {
        const std::size_t kind = static_cast<std::size_t>(id.kind);
        std::size_t seed = std::hash<std::uint64_t>{}(id.value) ^ (kind << 1);
        seed ^= std::hash<std::string>{}(id.scope) + 0x9e3779b9 + (seed << 6)
            + (seed >> 2);
        return seed;
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

    bool CombatResolver::previewRelocations(
        const std::vector<CombatantRelocation>& relocations) const
    {
        std::unordered_set<CombatantId, CombatantIdHash> sources;
        std::unordered_set<CombatantId, CombatantIdHash> destinations;
        sources.reserve(relocations.size());
        destinations.reserve(relocations.size());
        for (const CombatantRelocation& relocation : relocations)
        {
            if (!validId(relocation.source) || !validId(relocation.destination)
                || relocation.source.kind != CombatantKind::Actor
                || relocation.destination.kind != CombatantKind::Actor
                || relocation.source.value != relocation.destination.value
                || relocation.source == relocation.destination
                || !validPosition(relocation.position)
                || !sources.insert(relocation.source).second
                || !destinations.insert(relocation.destination).second
                || mCombatants.contains(relocation.destination))
            {
                return false;
            }
        }
        return true;
    }

    bool CombatResolver::applyRelocations(
        const std::vector<CombatantRelocation>& relocations)
    {
        if (!previewRelocations(relocations))
            return false;

        auto combatants = mCombatants;
        auto sequences = mSequences;
        for (const CombatantRelocation& relocation : relocations)
        {
            const auto source = combatants.find(relocation.source);
            if (source == combatants.end())
                continue;

            CombatantState state = source->second;
            state.position = relocation.position;
            combatants.emplace(relocation.destination, std::move(state));
            combatants.erase(source);

            const auto sequence = sequences.find(relocation.source);
            if (sequence != sequences.end())
            {
                sequences.emplace(relocation.destination, sequence->second);
                sequences.erase(sequence);
            }
        }
        mCombatants.swap(combatants);
        mSequences.swap(sequences);
        return true;
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

    void CombatResolver::swap(CombatResolver& other) noexcept
    {
        std::swap(mMaximumCombatants, other.mMaximumCombatants);
        mCombatants.swap(other.mCombatants);
        mSequences.swap(other.mSequences);
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

    bool CombatResolver::validId(const CombatantId& id) noexcept
    {
        if (id.value == 0)
            return false;
        return id.kind != CombatantKind::Actor || !id.scope.empty();
    }

    bool CombatResolver::validState(const CombatantState& state) noexcept
    {
        const auto finiteRange = [](double value) {
            return std::isfinite(value) && value >= 0 && value <= MaximumStatValue;
        };
        const double expectedFatigueRatio = state.maximumFatigue == 0
            ? 1.0 : state.fatigue / state.maximumFatigue;
        return finiteRange(state.health) && finiteRange(state.maximumHealth)
            && state.health <= state.maximumHealth
            && finiteRange(state.fatigue) && finiteRange(state.maximumFatigue)
            && state.fatigue <= state.maximumFatigue
            && std::isfinite(state.fatigueRatio) && state.fatigueRatio >= 0
            && state.fatigueRatio <= 1
            && std::abs(state.fatigueRatio - expectedFatigueRatio) <= 0.000001
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
