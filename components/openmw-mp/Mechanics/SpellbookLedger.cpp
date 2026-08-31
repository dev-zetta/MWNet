#include "SpellbookLedger.hpp"

#include <algorithm>

namespace mwmp::mechanics
{
    SpellbookLedger::SpellbookLedger(std::size_t maximumOwners)
        : mMaximumOwners(maximumOwners)
    {
    }

    SpellbookResult SpellbookLedger::preview(std::uint64_t owner,
        SpellbookAction action, const std::vector<std::string>& spellIds) const
    {
        if (owner == 0)
            return { SpellbookDecision::InvalidOwner };
        const auto existing = mSpellbooks.find(owner);
        if (existing == mSpellbooks.end() && mSpellbooks.size() >= mMaximumOwners)
            return { SpellbookDecision::OwnerLimitReached };
        SpellSet candidate;
        if (existing != mSpellbooks.end())
            candidate = existing->second;
        return applyTo(candidate, action, spellIds);
    }

    SpellbookResult SpellbookLedger::apply(std::uint64_t owner,
        SpellbookAction action, const std::vector<std::string>& spellIds)
    {
        if (owner == 0)
            return { SpellbookDecision::InvalidOwner };
        const auto existing = mSpellbooks.find(owner);
        if (existing == mSpellbooks.end() && mSpellbooks.size() >= mMaximumOwners)
            return { SpellbookDecision::OwnerLimitReached };
        SpellSet candidate;
        if (existing != mSpellbooks.end())
            candidate = existing->second;
        const SpellbookResult result = applyTo(candidate, action, spellIds);
        if (!result.applied())
            return result;
        mSpellbooks.insert_or_assign(owner, std::move(candidate));
        return result;
    }

    bool SpellbookLedger::contains(
        std::uint64_t owner, const std::string& spellId) const
    {
        const auto found = mSpellbooks.find(owner);
        return found != mSpellbooks.end() && found->second.contains(spellId);
    }

    std::optional<std::vector<std::string>> SpellbookLedger::snapshot(
        std::uint64_t owner) const
    {
        const auto found = mSpellbooks.find(owner);
        if (found == mSpellbooks.end())
            return std::nullopt;
        std::vector<std::string> result(found->second.begin(), found->second.end());
        std::ranges::sort(result);
        return result;
    }

    bool SpellbookLedger::erase(std::uint64_t owner) noexcept
    {
        return mSpellbooks.erase(owner) != 0;
    }

    void SpellbookLedger::clear() noexcept
    {
        mSpellbooks.clear();
    }

    std::size_t SpellbookLedger::size() const noexcept
    {
        return mSpellbooks.size();
    }

    bool SpellbookLedger::validSpellId(const std::string& spellId) noexcept
    {
        if (spellId.empty() || spellId.size() > MaximumStringBytes)
            return false;
        return std::ranges::none_of(spellId, [](unsigned char character) {
            return character == 0 || character < 0x20 || character == 0x7f;
        });
    }

    SpellbookResult SpellbookLedger::applyTo(SpellSet& spellbook,
        SpellbookAction action, const std::vector<std::string>& spellIds)
    {
        if (spellIds.size() > MaximumSpells)
            return { SpellbookDecision::SpellLimitReached, spellbook.size() };
        SpellSet unique;
        unique.reserve(spellIds.size());
        for (const std::string& spellId : spellIds)
        {
            if (!validSpellId(spellId) || !unique.insert(spellId).second)
                return { SpellbookDecision::InvalidSpell, spellbook.size() };
        }

        switch (action)
        {
            case SpellbookAction::Set:
                spellbook = std::move(unique);
                break;
            case SpellbookAction::Add:
            {
                const std::size_t added = std::ranges::count_if(unique,
                    [&spellbook](const std::string& spellId) {
                        return !spellbook.contains(spellId);
                    });
                if (added > MaximumSpells - spellbook.size())
                    return { SpellbookDecision::SpellLimitReached, spellbook.size() };
                spellbook.insert(unique.begin(), unique.end());
                break;
            }
            case SpellbookAction::Remove:
                for (const std::string& spellId : unique)
                {
                    if (!spellbook.contains(spellId))
                        return { SpellbookDecision::MissingSpell, spellbook.size() };
                }
                for (const std::string& spellId : unique)
                    spellbook.erase(spellId);
                break;
            default:
                return { SpellbookDecision::InvalidAction, spellbook.size() };
        }
        if (spellbook.size() > MaximumSpells)
            return { SpellbookDecision::SpellLimitReached, spellbook.size() };
        return { SpellbookDecision::Applied, spellbook.size() };
    }

    const char* describe(SpellbookDecision decision) noexcept
    {
        switch (decision)
        {
            case SpellbookDecision::Applied: return "the spellbook change was applied";
            case SpellbookDecision::InvalidOwner: return "the spellbook owner is invalid";
            case SpellbookDecision::InvalidAction: return "the spellbook action is invalid";
            case SpellbookDecision::InvalidSpell: return "a spell identifier is invalid or duplicated";
            case SpellbookDecision::MissingSpell: return "the spellbook does not contain the spell";
            case SpellbookDecision::SpellLimitReached: return "the spellbook limit was reached";
            case SpellbookDecision::OwnerLimitReached: return "the spellbook owner limit was reached";
        }
        return "unknown spellbook decision";
    }
}
