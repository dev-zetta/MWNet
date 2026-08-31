#include "SpellResolver.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace mwmp::mechanics
{
    SpellResolver::SpellResolver(
        std::size_t maximumCombatants, std::size_t maximumDefinitions)
        : mMaximumCombatants(maximumCombatants)
        , mMaximumDefinitions(maximumDefinitions)
    {
    }

    bool SpellResolver::upsertCombatant(
        CombatantId id, const SpellCombatantState& state)
    {
        if (!validId(id) || !validState(state))
            return false;
        if (!mCombatants.contains(id) && mCombatants.size() >= mMaximumCombatants)
            return false;
        mCombatants.insert_or_assign(std::move(id), state);
        return true;
    }

    bool SpellResolver::upsertDefinition(SpellDefinition definition)
    {
        if (!validDefinition(definition))
            return false;
        if (!mDefinitions.contains(definition.id)
            && mDefinitions.size() >= mMaximumDefinitions)
        {
            return false;
        }
        const std::string id = definition.id;
        mDefinitions.insert_or_assign(id, std::move(definition));
        return true;
    }

    SpellResult SpellResolver::resolve(const SpellCastIntent& intent,
        double successRoll, double magnitudeRoll)
    {
        SpellResult result;
        result.sequence = intent.sequence;
        if (!validId(intent.caster)
            || (intent.target && !validId(*intent.target)))
        {
            result.decision = SpellDecision::InvalidEntity;
            return result;
        }
        if (intent.sequence == 0)
        {
            result.decision = SpellDecision::InvalidSequence;
            return result;
        }
        if (!validString(intent.sourceId) || !validRoll(successRoll)
            || !validRoll(magnitudeRoll))
        {
            result.decision = SpellDecision::InvalidIntent;
            return result;
        }

        const auto casterIt = mCombatants.find(intent.caster);
        const auto definitionIt = mDefinitions.find(intent.sourceId);
        if (casterIt == mCombatants.end())
        {
            result.decision = SpellDecision::InvalidEntity;
            return result;
        }
        if (definitionIt == mDefinitions.end())
        {
            result.decision = SpellDecision::UnknownSpell;
            return result;
        }
        const std::uint64_t previousSequence = mSequences[intent.caster];
        if (intent.sequence <= previousSequence)
        {
            result.decision = SpellDecision::StaleSequence;
            return result;
        }

        SpellCombatantState& caster = casterIt->second;
        if (!validState(caster))
        {
            result.decision = SpellDecision::InvalidState;
            return result;
        }
        if (!caster.alive || caster.health <= 0)
        {
            result.decision = SpellDecision::CasterDead;
            return result;
        }

        const SpellDefinition& definition = definitionIt->second;
        SpellCombatantState* target = nullptr;
        CombatantId targetId = intent.caster;
        if (definition.range == SpellRange::Self)
        {
            if (intent.target && *intent.target != intent.caster)
            {
                result.decision = SpellDecision::UnexpectedTarget;
                return result;
            }
            target = &caster;
        }
        else
        {
            if (!intent.target)
            {
                result.decision = SpellDecision::MissingTarget;
                return result;
            }
            targetId = *intent.target;
            const auto targetIt = mCombatants.find(targetId);
            if (targetIt == mCombatants.end())
            {
                result.decision = SpellDecision::InvalidEntity;
                return result;
            }
            target = &targetIt->second;
            if (!validState(*target))
            {
                result.decision = SpellDecision::InvalidState;
                return result;
            }
            if (!target->alive || target->health <= 0)
            {
                result.decision = SpellDecision::TargetDead;
                return result;
            }
            if (distance(caster.position, target->position) > definition.maximumRange)
            {
                result.decision = SpellDecision::OutOfRange;
                return result;
            }
        }

        if (caster.magicka < definition.magickaCost)
        {
            result.decision = SpellDecision::InsufficientMagicka;
            return result;
        }

        // A syntactically and semantically valid cast consumes its sequence and
        // magicka even when the server's success roll fails.
        mSequences.insert_or_assign(intent.caster, intent.sequence);
        caster.magicka -= definition.magickaCost;
        result.magickaSpent = definition.magickaCost;
        result.successChance = definition.alwaysSucceeds
            ? 1.0
            : std::clamp(definition.baseSuccessChance * caster.castingMultiplier,
                0.0, 1.0);
        if (!definition.alwaysSucceeds && successRoll >= result.successChance)
        {
            result.decision = SpellDecision::Failed;
            result.targetHealth = target->health;
            return result;
        }

        CanonicalActiveSpell activeSpell;
        activeSpell.id = definition.id;
        activeSpell.displayName = definition.displayName;
        activeSpell.stacking = definition.stacking;
        activeSpell.caster = intent.caster;
        double healthDelta = 0;
        for (const SpellEffectDefinition& effect : definition.effects)
        {
            const double magnitude = effect.minimumMagnitude
                + (effect.maximumMagnitude - effect.minimumMagnitude) * magnitudeRoll;
            const double resistedMagnitude = effect.kind == SpellEffectKind::DamageHealth
                ? magnitude * (1.0 - target->resistance)
                : magnitude;
            if (effect.kind == SpellEffectKind::DamageHealth)
            {
                healthDelta -= resistedMagnitude;
                continue;
            }
            if (effect.kind == SpellEffectKind::RestoreHealth)
            {
                healthDelta += magnitude;
                continue;
            }
            activeSpell.effects.push_back({ effect.effectId, effect.argument,
                resistedMagnitude, effect.duration, effect.duration });
        }

        target->health = std::clamp(
            target->health + healthDelta, 0.0, target->maximumHealth);
        target->alive = target->health > 0;
        result.targetHealth = target->health;
        result.targetDied = !target->alive;
        if (!activeSpell.effects.empty())
            result.activeSpell = std::move(activeSpell);
        result.decision = SpellDecision::Applied;
        return result;
    }

    std::optional<SpellCombatantState> SpellResolver::findCombatant(
        CombatantId id) const
    {
        const auto it = mCombatants.find(id);
        if (it == mCombatants.end())
            return std::nullopt;
        return it->second;
    }

    std::optional<SpellDefinition> SpellResolver::findDefinition(
        const std::string& id) const
    {
        const auto it = mDefinitions.find(id);
        if (it == mDefinitions.end())
            return std::nullopt;
        return it->second;
    }

    bool SpellResolver::eraseCombatant(CombatantId id) noexcept
    {
        mSequences.erase(id);
        return mCombatants.erase(id) != 0;
    }

    bool SpellResolver::eraseDefinition(const std::string& id) noexcept
    {
        return mDefinitions.erase(id) != 0;
    }

    void SpellResolver::clear() noexcept
    {
        mSequences.clear();
        mCombatants.clear();
        mDefinitions.clear();
    }

    bool SpellResolver::validId(const CombatantId& id) noexcept
    {
        if (id.value == 0)
            return false;
        return id.kind == CombatantKind::Player ? id.scope.empty() : !id.scope.empty();
    }

    bool SpellResolver::validState(const SpellCombatantState& state) noexcept
    {
        const auto validStat = [](double value) {
            return std::isfinite(value) && value >= 0 && value <= MaximumStatValue;
        };
        const auto validCoordinate = [](double value) {
            return std::isfinite(value) && std::abs(value) <= MaximumStatValue;
        };
        return validStat(state.health) && validStat(state.maximumHealth)
            && state.health <= state.maximumHealth && validStat(state.magicka)
            && validStat(state.maximumMagicka)
            && state.magicka <= state.maximumMagicka
            && std::isfinite(state.castingMultiplier)
            && state.castingMultiplier >= 0
            && state.castingMultiplier <= MaximumStatValue
            && std::isfinite(state.resistance) && state.resistance >= 0
            && state.resistance <= 1 && validCoordinate(state.position.x)
            && validCoordinate(state.position.y) && validCoordinate(state.position.z)
            && state.alive == (state.health > 0);
    }

    bool SpellResolver::validDefinition(const SpellDefinition& definition) noexcept
    {
        const auto validNonNegative = [](double value, double maximum) {
            return std::isfinite(value) && value >= 0 && value <= maximum;
        };
        if (!validString(definition.id) || !validString(definition.displayName)
            || !validNonNegative(definition.magickaCost, MaximumStatValue)
            || !validNonNegative(definition.baseSuccessChance, 1)
            || !validNonNegative(definition.maximumRange, MaximumTargetRange)
            || definition.effects.empty()
            || definition.effects.size() > MaximumEffectsPerSpell)
        {
            return false;
        }
        if (definition.range == SpellRange::Self && definition.maximumRange != 0)
            return false;
        if (definition.range != SpellRange::Self && definition.maximumRange == 0)
            return false;
        return std::ranges::all_of(definition.effects,
            [&validNonNegative](const SpellEffectDefinition& effect) {
                return validString(effect.effectId)
                    && (effect.argument.empty() || validString(effect.argument))
                    && validNonNegative(effect.minimumMagnitude, MaximumStatValue)
                    && validNonNegative(effect.maximumMagnitude, MaximumStatValue)
                    && effect.maximumMagnitude >= effect.minimumMagnitude
                    && validNonNegative(effect.duration, MaximumDurationSeconds)
                    && (effect.kind != SpellEffectKind::Timed || effect.duration > 0)
                    && (effect.kind == SpellEffectKind::Timed
                        || effect.duration == 0);
            });
    }

    bool SpellResolver::validString(const std::string& value) noexcept
    {
        if (value.empty() || value.size() > MaximumStringBytes)
            return false;
        return std::ranges::none_of(value, [](unsigned char character) {
            return character == 0 || character < 0x20 || character == 0x7f;
        });
    }

    bool SpellResolver::validRoll(double value) noexcept
    {
        return std::isfinite(value) && value >= 0 && value < 1;
    }

    double SpellResolver::distance(
        const Position3& left, const Position3& right) noexcept
    {
        return std::hypot(left.x - right.x, left.y - right.y, left.z - right.z);
    }

    const char* describe(SpellDecision decision) noexcept
    {
        switch (decision)
        {
            case SpellDecision::Applied: return "the server applied the spell";
            case SpellDecision::Failed: return "the server rejected the success roll";
            case SpellDecision::InvalidEntity: return "the cast references an unknown entity";
            case SpellDecision::InvalidSequence: return "the cast sequence is invalid";
            case SpellDecision::StaleSequence: return "the cast sequence is stale";
            case SpellDecision::InvalidIntent: return "the cast intent is invalid";
            case SpellDecision::InvalidState: return "the canonical spell state is invalid";
            case SpellDecision::UnknownSpell: return "the spell is not in the canonical registry";
            case SpellDecision::CasterDead: return "the caster is dead";
            case SpellDecision::TargetDead: return "the target is dead";
            case SpellDecision::MissingTarget: return "the spell requires a target";
            case SpellDecision::UnexpectedTarget: return "the self spell has an unexpected target";
            case SpellDecision::OutOfRange: return "the target is outside the server-approved spell range";
            case SpellDecision::InsufficientMagicka: return "the caster does not have enough magicka";
            case SpellDecision::CapacityReached: return "the canonical spell state reached its capacity";
        }
        return "unknown spell decision";
    }
}
