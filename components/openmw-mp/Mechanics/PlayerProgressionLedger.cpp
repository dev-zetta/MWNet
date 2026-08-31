#include "PlayerProgressionLedger.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace mwmp::mechanics
{
    PlayerProgressionLedger::PlayerProgressionLedger(std::size_t maximumPlayers)
        : mMaximumPlayers(maximumPlayers)
    {
    }

    ProgressionResult PlayerProgressionLedger::set(
        std::uint64_t player, PlayerProgressionState state)
    {
        if (player == 0)
            return { ProgressionDecision::InvalidPlayer, {} };
        if (!validState(state))
            return { ProgressionDecision::InvalidStat, {} };

        auto found = mPlayers.find(player);
        if (found == mPlayers.end())
        {
            if (mPlayers.size() >= mMaximumPlayers)
                return { ProgressionDecision::CapacityReached, {} };
            found = mPlayers.emplace(player, std::move(state)).first;
        }
        else
            found->second = std::move(state);
        return { ProgressionDecision::Applied, found->second };
    }

    ProgressionResult PlayerProgressionLedger::previewAttributes(
        std::uint64_t player, bool fullSnapshot,
        std::span<const AttributeProgressionChange> changes) const
    {
        if (player == 0)
            return { ProgressionDecision::InvalidPlayer, {} };
        const auto found = mPlayers.find(player);
        if (found == mPlayers.end())
            return { ProgressionDecision::UnknownPlayer, {} };
        if (fullSnapshot)
            return { ProgressionDecision::FullClientSnapshot, found->second };

        PlayerProgressionState candidate = found->second;
        std::array<bool, PlayerProgressionState::AttributeCount> seen{};
        std::size_t increasedAttributes = 0;
        double totalAttributeIncrease = 0;
        for (const AttributeProgressionChange& change : changes)
        {
            if (change.index >= candidate.attributes.size())
                return { ProgressionDecision::InvalidIndex, found->second };
            if (seen[change.index])
                return { ProgressionDecision::DuplicateIndex, found->second };
            seen[change.index] = true;
            if (!validStat(change.stat) || change.skillIncrease < 0
                || change.skillIncrease > MaximumSkillIncrease)
            {
                return { ProgressionDecision::InvalidStat, found->second };
            }

            const ProgressionStat& current = candidate.attributes[change.index];
            const double baseDelta
                = static_cast<double>(change.stat.base) - current.base;
            if (baseDelta > 0)
            {
                ++increasedAttributes;
                totalAttributeIncrease += baseDelta;
                if (candidate.levelProgress < 10 || baseDelta > 5
                    || increasedAttributes > 3 || totalAttributeIncrease > 15)
                {
                    return { ProgressionDecision::UnauthorizedAttributeChange,
                        found->second };
                }
            }
            else if (baseDelta < -1)
            {
                return { ProgressionDecision::UnauthorizedAttributeChange,
                    found->second };
            }

            const std::int32_t increaseDelta = change.skillIncrease
                - candidate.skillIncreases[change.index];
            if (increaseDelta > 1
                || (increaseDelta < 0 && candidate.levelProgress < 10))
            {
                return { ProgressionDecision::UnauthorizedAttributeChange,
                    found->second };
            }
            candidate.attributes[change.index] = change.stat;
            candidate.skillIncreases[change.index] = change.skillIncrease;
        }
        return { ProgressionDecision::Applied, std::move(candidate) };
    }

    ProgressionResult PlayerProgressionLedger::applyAttributes(
        std::uint64_t player, bool fullSnapshot,
        std::span<const AttributeProgressionChange> changes)
    {
        ProgressionResult result = previewAttributes(player, fullSnapshot, changes);
        if (result.applied())
            mPlayers.find(player)->second = result.state;
        return result;
    }

    ProgressionResult PlayerProgressionLedger::previewSkills(
        std::uint64_t player, bool fullSnapshot,
        std::span<const SkillProgressionChange> changes) const
    {
        if (player == 0)
            return { ProgressionDecision::InvalidPlayer, {} };
        const auto found = mPlayers.find(player);
        if (found == mPlayers.end())
            return { ProgressionDecision::UnknownPlayer, {} };
        if (fullSnapshot)
            return { ProgressionDecision::FullClientSnapshot, found->second };

        PlayerProgressionState candidate = found->second;
        std::array<bool, PlayerProgressionState::SkillCount> seen{};
        for (const SkillProgressionChange& change : changes)
        {
            if (change.index >= candidate.skills.size())
                return { ProgressionDecision::InvalidIndex, found->second };
            if (seen[change.index])
                return { ProgressionDecision::DuplicateIndex, found->second };
            seen[change.index] = true;
            if (!validStat(change.stat))
                return { ProgressionDecision::InvalidStat, found->second };

            const ProgressionStat& current = candidate.skills[change.index];
            const double baseDelta
                = static_cast<double>(change.stat.base) - current.base;
            const double progressDelta
                = static_cast<double>(change.stat.progress) - current.progress;
            if (baseDelta < -1 || baseDelta > 1 || progressDelta > 1000
                || (baseDelta == 0 && progressDelta < 0))
            {
                return { ProgressionDecision::UnauthorizedSkillChange,
                    found->second };
            }
            candidate.skills[change.index] = change.stat;
        }
        return { ProgressionDecision::Applied, std::move(candidate) };
    }

    ProgressionResult PlayerProgressionLedger::applySkills(
        std::uint64_t player, bool fullSnapshot,
        std::span<const SkillProgressionChange> changes)
    {
        ProgressionResult result = previewSkills(player, fullSnapshot, changes);
        if (result.applied())
            mPlayers.find(player)->second = result.state;
        return result;
    }

    ProgressionResult PlayerProgressionLedger::previewLevel(
        std::uint64_t player, std::int32_t level,
        std::int32_t levelProgress) const
    {
        if (player == 0)
            return { ProgressionDecision::InvalidPlayer, {} };
        const auto found = mPlayers.find(player);
        if (found == mPlayers.end())
            return { ProgressionDecision::UnknownPlayer, {} };
        if (level < 1 || level > MaximumLevel || levelProgress < 0
            || levelProgress > MaximumLevelProgress)
        {
            return { ProgressionDecision::InvalidStat, found->second };
        }

        const PlayerProgressionState& current = found->second;
        const bool progressOnly = level == current.level
            && levelProgress >= current.levelProgress
            && levelProgress - current.levelProgress <= 10;
        const bool levelUp = level == current.level + 1
            && current.levelProgress >= 10
            && levelProgress == current.levelProgress - 10;
        if (!progressOnly && !levelUp)
            return { ProgressionDecision::UnauthorizedLevelChange, current };

        PlayerProgressionState candidate = current;
        candidate.level = level;
        candidate.levelProgress = levelProgress;
        return { ProgressionDecision::Applied, std::move(candidate) };
    }

    ProgressionResult PlayerProgressionLedger::applyLevel(
        std::uint64_t player, std::int32_t level, std::int32_t levelProgress)
    {
        ProgressionResult result = previewLevel(player, level, levelProgress);
        if (result.applied())
            mPlayers.find(player)->second = result.state;
        return result;
    }

    std::optional<PlayerProgressionState> PlayerProgressionLedger::find(
        std::uint64_t player) const
    {
        const auto found = mPlayers.find(player);
        if (found == mPlayers.end())
            return std::nullopt;
        return found->second;
    }

    bool PlayerProgressionLedger::erase(std::uint64_t player) noexcept
    {
        return mPlayers.erase(player) != 0;
    }

    void PlayerProgressionLedger::clear() noexcept
    {
        mPlayers.clear();
    }

    std::size_t PlayerProgressionLedger::size() const noexcept
    {
        return mPlayers.size();
    }

    bool PlayerProgressionLedger::validStat(const ProgressionStat& stat) noexcept
    {
        const auto valid = [](float value) {
            return std::isfinite(value) && std::abs(value) <= MaximumStatValue;
        };
        return valid(stat.base) && stat.base >= 0 && valid(stat.modifier)
            && valid(stat.current) && valid(stat.damage) && valid(stat.progress)
            && stat.progress >= 0;
    }

    bool PlayerProgressionLedger::validState(
        const PlayerProgressionState& state) noexcept
    {
        if (state.level < 1 || state.level > MaximumLevel
            || state.levelProgress < 0
            || state.levelProgress > MaximumLevelProgress)
        {
            return false;
        }
        if (std::ranges::any_of(state.attributes,
                [](const ProgressionStat& stat) { return !validStat(stat); })
            || std::ranges::any_of(state.skills,
                [](const ProgressionStat& stat) { return !validStat(stat); }))
        {
            return false;
        }
        return std::ranges::all_of(state.skillIncreases, [](std::int32_t value) {
            return value >= 0 && value <= MaximumSkillIncrease;
        });
    }

    const char* describe(ProgressionDecision decision) noexcept
    {
        switch (decision)
        {
            case ProgressionDecision::Applied:
                return "the progression change was applied";
            case ProgressionDecision::InvalidPlayer:
                return "the progression change has an invalid player";
            case ProgressionDecision::UnknownPlayer:
                return "the player has no canonical progression state";
            case ProgressionDecision::InvalidStat:
                return "the progression value is outside the canonical bounds";
            case ProgressionDecision::InvalidIndex:
                return "the progression change has an invalid index";
            case ProgressionDecision::DuplicateIndex:
                return "the progression change repeats an index";
            case ProgressionDecision::FullClientSnapshot:
                return "clients may not replace the full progression state";
            case ProgressionDecision::UnauthorizedAttributeChange:
                return "the attribute change exceeds the validated progression bound";
            case ProgressionDecision::UnauthorizedSkillChange:
                return "the skill change exceeds the validated progression bound";
            case ProgressionDecision::UnauthorizedLevelChange:
                return "the level change is not supported by canonical progress";
            case ProgressionDecision::CapacityReached:
                return "the progression ledger is at capacity";
        }
        return "unknown progression decision";
    }
}
