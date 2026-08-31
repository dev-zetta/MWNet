#include "ActiveEffectLedger.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace mwmp::mechanics
{
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
}
