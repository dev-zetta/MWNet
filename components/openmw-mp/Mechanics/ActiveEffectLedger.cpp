#include "ActiveEffectLedger.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <unordered_set>
#include <utility>

namespace mwmp::mechanics
{
    namespace
    {
        std::string canonicalEffectId(std::string_view value)
        {
            std::string result;
            result.reserve(value.size());
            for (const unsigned char character : value)
            {
                if (character != ' ' && character != '_' && character != '-')
                    result.push_back(static_cast<char>(std::tolower(character)));
            }
            return result;
        }

        double healthRate(const CanonicalEffect& effect)
        {
            const std::string id = canonicalEffectId(effect.effectId);
            if (id == "restorehealth")
                return effect.magnitude;
            if (id == "damagehealth" || id == "firedamage"
                || id == "frostdamage" || id == "shockdamage"
                || id == "poison" || id == "sundamage"
                || id == "absorbhealth")
            {
                return -effect.magnitude;
            }
            return 0;
        }

        bool isAbsorbHealth(const CanonicalEffect& effect)
        {
            return canonicalEffectId(effect.effectId) == "absorbhealth";
        }

        ActiveEffectTick& changeFor(std::vector<ActiveEffectTick>& changes,
            const CombatantId& owner)
        {
            const auto existing = std::ranges::find(changes, owner,
                &ActiveEffectTick::owner);
            if (existing != changes.end())
                return *existing;
            ActiveEffectTick change;
            change.owner = owner;
            changes.push_back(std::move(change));
            return changes.back();
        }
    }

    ActiveEffectLedger::ActiveEffectLedger(std::size_t maximumOwners)
        : mMaximumOwners(maximumOwners)
    {
    }

    ActiveEffectResult ActiveEffectLedger::preview(CombatantId owner,
        ActiveEffectAction action, const std::vector<CanonicalActiveSpell>& spells) const
    {
        if (!validOwner(owner))
            return { ActiveEffectDecision::InvalidOwner };
        const auto existing = mActiveEffects.find(owner);
        if (existing == mActiveEffects.end() && mActiveEffects.size() >= mMaximumOwners)
            return { ActiveEffectDecision::OwnerLimitReached };

        std::vector<CanonicalActiveSpell> candidate;
        if (existing != mActiveEffects.end())
            candidate = existing->second;
        return applyTo(candidate, action, spells);
    }

    ActiveEffectResult ActiveEffectLedger::apply(CombatantId owner,
        ActiveEffectAction action, const std::vector<CanonicalActiveSpell>& spells)
    {
        if (!validOwner(owner))
            return { ActiveEffectDecision::InvalidOwner };
        const auto existing = mActiveEffects.find(owner);
        if (existing == mActiveEffects.end() && mActiveEffects.size() >= mMaximumOwners)
            return { ActiveEffectDecision::OwnerLimitReached };

        std::vector<CanonicalActiveSpell> candidate;
        if (existing != mActiveEffects.end())
            candidate = existing->second;
        const ActiveEffectResult result = applyTo(candidate, action, spells);
        if (!result.applied())
            return result;
        mActiveEffects.insert_or_assign(std::move(owner), std::move(candidate));
        return result;
    }

    ActiveEffectResult ActiveEffectLedger::previewBatch(
        const std::vector<ActiveEffectOperation>& operations) const
    {
        std::unordered_map<CombatantId, std::vector<CanonicalActiveSpell>, CombatantIdHash>
            candidates;
        return prepareBatch(operations, candidates);
    }

    ActiveEffectResult ActiveEffectLedger::applyBatch(
        const std::vector<ActiveEffectOperation>& operations)
    {
        std::unordered_map<CombatantId, std::vector<CanonicalActiveSpell>, CombatantIdHash>
            candidates;
        const ActiveEffectResult result = prepareBatch(operations, candidates);
        if (!result.applied())
            return result;
        for (auto& [owner, spells] : candidates)
            mActiveEffects.insert_or_assign(std::move(owner), std::move(spells));
        return result;
    }

    bool ActiveEffectLedger::previewRelocations(
        const std::vector<CombatantRelocation>& relocations) const
    {
        std::unordered_set<CombatantId, CombatantIdHash> sources;
        std::unordered_set<CombatantId, CombatantIdHash> destinations;
        sources.reserve(relocations.size());
        destinations.reserve(relocations.size());
        for (const CombatantRelocation& relocation : relocations)
        {
            if (!validOwner(relocation.source) || !validOwner(relocation.destination)
                || relocation.source.kind != CombatantKind::Actor
                || relocation.destination.kind != CombatantKind::Actor
                || relocation.source.value != relocation.destination.value
                || relocation.source == relocation.destination
                || !sources.insert(relocation.source).second
                || !destinations.insert(relocation.destination).second
                || mActiveEffects.contains(relocation.destination))
            {
                return false;
            }
        }
        return true;
    }

    bool ActiveEffectLedger::applyRelocations(
        const std::vector<CombatantRelocation>& relocations)
    {
        if (!previewRelocations(relocations))
            return false;

        auto activeEffects = mActiveEffects;
        for (const CombatantRelocation& relocation : relocations)
        {
            const auto source = activeEffects.find(relocation.source);
            if (source != activeEffects.end())
            {
                activeEffects.emplace(relocation.destination, source->second);
                activeEffects.erase(source);
            }

            for (auto& [owner, spells] : activeEffects)
            {
                for (CanonicalActiveSpell& spell : spells)
                {
                    if (spell.caster && *spell.caster == relocation.source)
                        spell.caster = relocation.destination;
                }
            }
        }
        mActiveEffects.swap(activeEffects);
        return true;
    }

    ActiveEffectAdvanceResult ActiveEffectLedger::advance(double elapsedSeconds)
    {
        ActiveEffectAdvanceResult result;
        if (!std::isfinite(elapsedSeconds) || elapsedSeconds < 0
            || elapsedSeconds > MaximumDurationSeconds)
        {
            return result;
        }
        result.decision = ActiveEffectAdvanceDecision::Applied;
        if (elapsedSeconds == 0)
            return result;

        auto candidate = mActiveEffects;
        for (auto ownerIt = candidate.begin(); ownerIt != candidate.end();)
        {
            const CombatantId owner = ownerIt->first;
            std::vector<CanonicalActiveSpell>& spells = ownerIt->second;
            bool ownerTopologyChanged = false;
            for (auto spellIt = spells.begin(); spellIt != spells.end();)
            {
                CanonicalActiveSpell& spell = *spellIt;
                for (auto effectIt = spell.effects.begin();
                    effectIt != spell.effects.end();)
                {
                    CanonicalEffect& effect = *effectIt;
                    const double appliedSeconds = std::min(
                        elapsedSeconds, effect.timeLeft);
                    const double rate = healthRate(effect);
                    if (rate != 0 && appliedSeconds > 0)
                    {
                        ActiveEffectTick& ownerChange
                            = changeFor(result.changes, owner);
                        ownerChange.healthDelta += rate * appliedSeconds;
                        if (rate < 0 && spell.caster
                            && !ownerChange.damageSource)
                        {
                            ownerChange.damageSource = spell.caster;
                        }
                        if (isAbsorbHealth(effect) && spell.caster
                            && *spell.caster != owner)
                        {
                            changeFor(result.changes, *spell.caster).healthDelta
                                -= rate * appliedSeconds;
                        }
                    }
                    effect.timeLeft = std::max(0.0,
                        effect.timeLeft - elapsedSeconds);
                    if (effect.timeLeft == 0)
                    {
                        effectIt = spell.effects.erase(effectIt);
                        ownerTopologyChanged = true;
                    }
                    else
                        ++effectIt;
                }
                if (spell.effects.empty())
                {
                    spellIt = spells.erase(spellIt);
                    ownerTopologyChanged = true;
                }
                else
                    ++spellIt;
            }
            if (ownerTopologyChanged)
                changeFor(result.changes, owner).topologyChanged = true;
            if (spells.empty())
                ownerIt = candidate.erase(ownerIt);
            else
                ++ownerIt;
        }
        mActiveEffects.swap(candidate);
        return result;
    }

    std::optional<std::vector<CanonicalActiveSpell>> ActiveEffectLedger::snapshot(
        CombatantId owner) const
    {
        const auto found = mActiveEffects.find(owner);
        if (found == mActiveEffects.end())
            return std::nullopt;
        return found->second;
    }

    bool ActiveEffectLedger::erase(CombatantId owner) noexcept
    {
        return mActiveEffects.erase(owner) != 0;
    }

    void ActiveEffectLedger::swap(ActiveEffectLedger& other) noexcept
    {
        std::swap(mMaximumOwners, other.mMaximumOwners);
        mActiveEffects.swap(other.mActiveEffects);
    }

    void ActiveEffectLedger::clear() noexcept
    {
        mActiveEffects.clear();
    }

    std::size_t ActiveEffectLedger::size() const noexcept
    {
        return mActiveEffects.size();
    }

    bool ActiveEffectLedger::validOwner(const CombatantId& owner) noexcept
    {
        if (owner.value == 0)
            return false;
        if (owner.kind == CombatantKind::Player)
            return owner.scope.empty();
        return !owner.scope.empty();
    }

    bool ActiveEffectLedger::validSpell(const CanonicalActiveSpell& spell) noexcept
    {
        if (spell.id.empty() || spell.id.size() > MaximumStringBytes
            || spell.displayName.size() > MaximumStringBytes || spell.timestampDay < 0
            || !std::isfinite(spell.timestampHour)
            || std::abs(spell.timestampHour) > MaximumDurationSeconds
            || spell.effects.size() > MaximumEffectsPerSpell)
        {
            return false;
        }
        if (spell.caster && !validOwner(*spell.caster))
            return false;
        return std::ranges::all_of(spell.effects, [](const CanonicalEffect& effect) {
            return !effect.effectId.empty() && effect.effectId.size() <= MaximumStringBytes
                && effect.argument.size() <= MaximumStringBytes
                && std::isfinite(effect.magnitude)
                && std::abs(effect.magnitude) <= MaximumMagnitude
                && std::isfinite(effect.duration) && effect.duration >= 0
                && effect.duration <= MaximumDurationSeconds
                && std::isfinite(effect.timeLeft) && effect.timeLeft >= 0
                && effect.timeLeft <= effect.duration;
        });
    }

    ActiveEffectResult ActiveEffectLedger::applyTo(
        std::vector<CanonicalActiveSpell>& active, ActiveEffectAction action,
        const std::vector<CanonicalActiveSpell>& spells)
    {
        if (spells.size() > MaximumActiveSpells)
            return { ActiveEffectDecision::SpellLimitReached, active.size() };
        switch (action)
        {
            case ActiveEffectAction::Set:
            {
                std::vector<CanonicalActiveSpell> replacement;
                const ActiveEffectResult result = addTo(replacement, spells);
                if (result.applied())
                    active = std::move(replacement);
                return result;
            }
            case ActiveEffectAction::Add:
                return addTo(active, spells);
            case ActiveEffectAction::Remove:
                return removeFrom(active, spells);
        }
        return { ActiveEffectDecision::InvalidAction, active.size() };
    }

    ActiveEffectResult ActiveEffectLedger::addTo(
        std::vector<CanonicalActiveSpell>& active,
        const std::vector<CanonicalActiveSpell>& spells)
    {
        for (const CanonicalActiveSpell& spell : spells)
        {
            if (!validSpell(spell))
                return { ActiveEffectDecision::InvalidSpell, active.size() };
            const auto existing = std::find_if(active.begin(), active.end(),
                [&spell](const CanonicalActiveSpell& candidate) {
                    return candidate.id == spell.id && !spell.stacking;
                });
            if (existing != active.end())
            {
                *existing = spell;
                continue;
            }
            if (active.size() >= MaximumActiveSpells)
                return { ActiveEffectDecision::SpellLimitReached, active.size() };
            active.push_back(spell);
        }
        return { ActiveEffectDecision::Applied, active.size() };
    }

    ActiveEffectResult ActiveEffectLedger::removeFrom(
        std::vector<CanonicalActiveSpell>& active,
        const std::vector<CanonicalActiveSpell>& spells)
    {
        for (const CanonicalActiveSpell& spell : spells)
        {
            // Protocol remove entries are selectors, not full active-spell
            // snapshots. Do not require their effect list to be repeated.
            if (spell.id.empty() || spell.id.size() > MaximumStringBytes)
                return { ActiveEffectDecision::InvalidSpell, active.size() };
            const auto existing = std::find_if(active.begin(), active.end(),
                [&spell](const CanonicalActiveSpell& candidate) {
                    return candidate.id == spell.id;
                });
            if (existing == active.end())
                return { ActiveEffectDecision::MissingSpell, active.size() };
            active.erase(existing);
        }
        return { ActiveEffectDecision::Applied, active.size() };
    }

    ActiveEffectResult ActiveEffectLedger::prepareBatch(
        const std::vector<ActiveEffectOperation>& operations,
        std::unordered_map<CombatantId, std::vector<CanonicalActiveSpell>,
            CombatantIdHash>& candidates) const
    {
        ActiveEffectResult result{ ActiveEffectDecision::Applied };
        std::size_t newOwnerCount = 0;
        for (const ActiveEffectOperation& operation : operations)
        {
            if (!validOwner(operation.owner))
                return { ActiveEffectDecision::InvalidOwner };

            auto candidate = candidates.find(operation.owner);
            if (candidate == candidates.end())
            {
                const auto existing = mActiveEffects.find(operation.owner);
                if (existing == mActiveEffects.end()
                    && mActiveEffects.size() + newOwnerCount >= mMaximumOwners)
                {
                    return { ActiveEffectDecision::OwnerLimitReached };
                }
                if (existing == mActiveEffects.end())
                    ++newOwnerCount;
                candidate = candidates.emplace(operation.owner,
                    existing == mActiveEffects.end()
                        ? std::vector<CanonicalActiveSpell>{}
                        : existing->second).first;
            }

            result = applyTo(candidate->second, operation.action, operation.spells);
            if (!result.applied())
                return result;
        }
        return result;
    }

    const char* describe(ActiveEffectDecision decision) noexcept
    {
        switch (decision)
        {
            case ActiveEffectDecision::Applied:
                return "the active-effect change was applied";
            case ActiveEffectDecision::InvalidOwner:
                return "the active-effect owner is invalid";
            case ActiveEffectDecision::InvalidAction:
                return "the active-effect action is invalid";
            case ActiveEffectDecision::InvalidSpell:
                return "an active spell or effect is invalid";
            case ActiveEffectDecision::MissingSpell:
                return "the active spell does not exist";
            case ActiveEffectDecision::SpellLimitReached:
                return "the active-spell limit was reached";
            case ActiveEffectDecision::OwnerLimitReached:
                return "the active-effect owner limit was reached";
        }
        return "unknown active-effect decision";
    }

    const char* describe(ActiveEffectAdvanceDecision decision) noexcept
    {
        switch (decision)
        {
            case ActiveEffectAdvanceDecision::Applied:
                return "the active-effect clock advanced";
            case ActiveEffectAdvanceDecision::InvalidElapsed:
                return "the active-effect elapsed time is invalid";
        }
        return "unknown active-effect advance decision";
    }
}
