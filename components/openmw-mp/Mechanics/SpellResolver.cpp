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
        const bool requiresTarget = std::ranges::any_of(definition.effects,
            [](const SpellEffectDefinition& effect) {
                return effect.range != SpellRange::Self;
            });
        SpellCombatantState* externalTarget = nullptr;
        CombatantId externalTargetId = intent.caster;
        if (!requiresTarget)
        {
            if (intent.target && *intent.target != intent.caster)
            {
                result.decision = SpellDecision::UnexpectedTarget;
                return result;
            }
        }
        else
        {
            if (!intent.target)
            {
                result.decision = SpellDecision::MissingTarget;
                return result;
            }
            externalTargetId = *intent.target;
            const auto targetIt = mCombatants.find(externalTargetId);
            if (targetIt == mCombatants.end())
            {
                result.decision = SpellDecision::InvalidEntity;
                return result;
            }
            externalTarget = &targetIt->second;
            if (!validState(*externalTarget))
            {
                result.decision = SpellDecision::InvalidState;
                return result;
            }
            if (!externalTarget->alive || externalTarget->health <= 0)
            {
                result.decision = SpellDecision::TargetDead;
                return result;
            }
            const double targetDistance
                = distance(caster.position, externalTarget->position);
            const bool outOfRange = std::ranges::any_of(definition.effects,
                [targetDistance](const SpellEffectDefinition& effect) {
                    return effect.range != SpellRange::Self
                        && targetDistance > effect.maximumRange;
                });
            if (outOfRange)
            {
                result.decision = SpellDecision::OutOfRange;
                return result;
            }
        }

        if (definition.sourceKind == SpellSourceKind::Regular
            && caster.magicka < definition.magickaCost)
        {
            result.decision = SpellDecision::InsufficientMagicka;
            return result;
        }
        const double effectiveItemChargeCost
            = definition.sourceKind == SpellSourceKind::Item
                && definition.itemChargeCost > 0
            ? std::max(1.0, definition.itemChargeCost
                - (definition.itemChargeCost / 100.0)
                    * (caster.enchantSkill - 10.0))
            : 0.0;
        if (definition.sourceKind == SpellSourceKind::Item
            && (!intent.availableItemCharge
                || !std::isfinite(*intent.availableItemCharge)
                || *intent.availableItemCharge < effectiveItemChargeCost))
        {
            result.decision = SpellDecision::InsufficientItemCharge;
            return result;
        }

        // A syntactically and semantically valid cast consumes its sequence and
        // magicka even when the server's success roll fails.
        mSequences.insert_or_assign(intent.caster, intent.sequence);
        if (definition.sourceKind == SpellSourceKind::Regular)
        {
            caster.magicka -= definition.magickaCost;
            result.magickaSpent = definition.magickaCost;
        }
        else
            result.itemChargeSpent = effectiveItemChargeCost;

        result.successChance = definition.alwaysSucceeds ? 1.0 : 0.0;
        if (!definition.alwaysSucceeds && !caster.silenced)
        {
            double lowestDifference = std::numeric_limits<double>::max();
            double lowestSkill = 0;
            bool hasCastingSchool = false;
            for (const SpellEffectDefinition& effect : definition.effects)
            {
                if (effect.castingSchool.empty())
                    continue;
                hasCastingSchool = true;
                const auto skillIt = caster.magicSkills.find(effect.castingSchool);
                const double skill = skillIt == caster.magicSkills.end()
                    ? 0.0 : skillIt->second;
                const double doubledSkill = 2.0 * skill;
                const double difference
                    = doubledSkill - effect.castingDifficulty;
                if (difference < lowestDifference)
                {
                    lowestDifference = difference;
                    lowestSkill = doubledSkill;
                    result.effectiveSchool = effect.castingSchool;
                }
            }
            const double baseChance = hasCastingSchool
                ? lowestSkill - definition.magickaCost
                    + 0.2 * caster.willpower + 0.1 * caster.luck
                    - caster.soundMagnitude
                : definition.baseSuccessChance * 100.0;
            result.successChance = std::clamp(baseChance
                * caster.fatigueTerm * caster.castingMultiplier / 100.0,
                0.0, 1.0);
        }
        if (!definition.alwaysSucceeds && successRoll >= result.successChance)
        {
            result.decision = SpellDecision::Failed;
            result.targetHealth = externalTarget != nullptr
                ? externalTarget->health : caster.health;
            return result;
        }

        struct PendingApplication
        {
            CombatantId id;
            SpellCombatantState* state = nullptr;
            CanonicalActiveSpell activeSpell;
            double healthDelta = 0;
        };
        PendingApplication selfApplication{ intent.caster, &caster, {}, 0 };
        PendingApplication targetApplication{
            externalTargetId, externalTarget, {}, 0 };
        const auto initializeActiveSpell = [&definition, &intent](
            CanonicalActiveSpell& spell) {
            spell.id = definition.id;
            spell.displayName = definition.displayName;
            spell.stacking = definition.stacking;
            spell.caster = intent.caster;
        };
        initializeActiveSpell(selfApplication.activeSpell);
        initializeActiveSpell(targetApplication.activeSpell);

        for (const SpellEffectDefinition& effect : definition.effects)
        {
            PendingApplication& application = effect.range == SpellRange::Self
                ? selfApplication : targetApplication;
            const double magnitude = effect.minimumMagnitude
                + (effect.maximumMagnitude - effect.minimumMagnitude) * magnitudeRoll;
            const double resistedMagnitude = effect.kind == SpellEffectKind::DamageHealth
                ? magnitude * (1.0 - application.state->resistance)
                : magnitude;
            if (effect.kind == SpellEffectKind::DamageHealth)
            {
                application.healthDelta -= resistedMagnitude;
                continue;
            }
            if (effect.kind == SpellEffectKind::RestoreHealth)
            {
                application.healthDelta += magnitude;
                continue;
            }
            if (effect.kind == SpellEffectKind::Instant)
                continue;
            application.activeSpell.effects.push_back({ effect.effectId, effect.argument,
                resistedMagnitude, effect.duration, effect.duration });
        }

        const auto apply = [&result](PendingApplication& pending) {
            if (pending.state == nullptr)
                return;
            if (pending.healthDelta == 0 && pending.activeSpell.effects.empty())
                return;
            pending.state->health = std::clamp(pending.state->health
                + pending.healthDelta, 0.0, pending.state->maximumHealth);
            pending.state->alive = pending.state->health > 0;
            SpellApplication application;
            application.target = pending.id;
            application.health = pending.state->health;
            application.died = !pending.state->alive;
            if (!pending.activeSpell.effects.empty())
                application.activeSpell = std::move(pending.activeSpell);
            result.applications.push_back(std::move(application));
        };
        apply(selfApplication);
        apply(targetApplication);

        const SpellApplication* primary = nullptr;
        if (requiresTarget)
        {
            const auto it = std::ranges::find(result.applications,
                externalTargetId, &SpellApplication::target);
            if (it != result.applications.end())
                primary = &*it;
        }
        if (primary == nullptr && !result.applications.empty())
            primary = &result.applications.front();
        if (primary != nullptr)
        {
            result.targetHealth = primary->health;
            result.targetDied = primary->died;
            result.activeSpell = primary->activeSpell;
        }
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
            && state.alive == (state.health > 0)
            && validStat(state.willpower) && validStat(state.luck)
            && std::isfinite(state.fatigueTerm) && state.fatigueTerm >= 0
            && state.fatigueTerm <= MaximumStatValue
            && validStat(state.soundMagnitude) && validStat(state.enchantSkill)
            && std::ranges::all_of(state.magicSkills,
                [&validStat](const auto& skill) {
                    return validString(skill.first) && validStat(skill.second);
                });
    }

    bool SpellResolver::validDefinition(const SpellDefinition& definition) noexcept
    {
        const auto validNonNegative = [](double value, double maximum) {
            return std::isfinite(value) && value >= 0 && value <= maximum;
        };
        if (!validString(definition.id) || !validString(definition.displayName)
            || !validNonNegative(definition.magickaCost, MaximumStatValue)
            || !validNonNegative(definition.itemChargeCost, MaximumStatValue)
            || !validNonNegative(definition.baseSuccessChance, 1)
            || definition.effects.empty()
            || definition.effects.size() > MaximumEffectsPerSpell)
        {
            return false;
        }
        if (definition.sourceKind == SpellSourceKind::Regular
            && definition.itemChargeCost != 0)
            return false;
        if (definition.sourceKind == SpellSourceKind::Item
            && definition.magickaCost != 0)
            return false;
        return std::ranges::all_of(definition.effects,
            [&validNonNegative](const SpellEffectDefinition& effect) {
                return validString(effect.effectId)
                    && (effect.argument.empty() || validString(effect.argument))
                    && (effect.castingSchool.empty()
                        || validString(effect.castingSchool))
                    && validNonNegative(effect.castingDifficulty, MaximumStatValue)
                    && validNonNegative(effect.minimumMagnitude, MaximumStatValue)
                    && validNonNegative(effect.maximumMagnitude, MaximumStatValue)
                    && effect.maximumMagnitude >= effect.minimumMagnitude
                    && validNonNegative(effect.duration, MaximumDurationSeconds)
                    && validNonNegative(effect.maximumRange, MaximumTargetRange)
                    && (effect.kind == SpellEffectKind::Timed
                        ? effect.duration > 0 : effect.duration == 0)
                    && (effect.range == SpellRange::Self
                        ? effect.maximumRange == 0 : effect.maximumRange > 0);
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
            case SpellDecision::InsufficientItemCharge: return "the item does not have enough canonical charge";
            case SpellDecision::CapacityReached: return "the canonical spell state reached its capacity";
        }
        return "unknown spell decision";
    }
}
