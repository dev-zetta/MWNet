#include "JusticeLedger.hpp"

#include <algorithm>
#include <limits>
#include <utility>

namespace mwmp::mechanics
{
    JusticeLedger::JusticeLedger(std::size_t maximumPlayers)
        : mMaximumPlayers(maximumPlayers)
    {
    }

    JusticeResult JusticeLedger::setBounty(
        std::uint64_t player, std::int64_t bounty)
    {
        if (player == 0)
            return { JusticeDecision::InvalidPlayer, {} };
        if (!validBounty(bounty))
            return { JusticeDecision::InvalidBounty, {} };

        auto found = mPlayers.find(player);
        if (found == mPlayers.end())
        {
            if (mPlayers.size() >= mMaximumPlayers)
                return { JusticeDecision::CapacityReached, {} };
            found = mPlayers.emplace(player, JusticeState{}).first;
        }
        found->second.bounty = bounty;
        return { JusticeDecision::Applied, found->second };
    }

    JusticeResult JusticeLedger::previewBountyIntent(
        std::uint64_t player, std::int64_t reportedBounty) const
    {
        if (player == 0)
            return { JusticeDecision::InvalidPlayer, {} };
        if (!validBounty(reportedBounty))
            return { JusticeDecision::InvalidBounty, {} };
        const auto found = mPlayers.find(player);
        if (found == mPlayers.end())
            return { JusticeDecision::InvalidPlayer, {} };
        if (reportedBounty < found->second.bounty)
            return { JusticeDecision::UnauthorizedBountyReduction, found->second };

        JusticeState candidate = found->second;
        candidate.bounty = reportedBounty;
        return { JusticeDecision::Applied, std::move(candidate) };
    }

    JusticeResult JusticeLedger::applyBountyIntent(
        std::uint64_t player, std::int64_t reportedBounty)
    {
        JusticeResult result = previewBountyIntent(player, reportedBounty);
        if (result.applied())
            mPlayers.find(player)->second = result.state;
        return result;
    }

    JusticeResult JusticeLedger::beginSentence(std::uint64_t player,
        std::uint32_t days, bool ignoreTeleportation, bool ignoreSkillIncreases,
        std::string progressText, std::string endText)
    {
        if (player == 0)
            return { JusticeDecision::InvalidPlayer, {} };
        if (days == 0 || days > MaximumJailDays || !validText(progressText)
            || !validText(endText))
        {
            return { JusticeDecision::InvalidSentence, {} };
        }
        auto found = mPlayers.find(player);
        if (found == mPlayers.end())
        {
            if (mPlayers.size() >= mMaximumPlayers)
                return { JusticeDecision::CapacityReached, {} };
            found = mPlayers.emplace(player, JusticeState{}).first;
        }
        if (found->second.sentence)
            return { JusticeDecision::SentenceAlreadyActive, found->second };
        if (mNextSentenceId == 0
            || mNextSentenceId == std::numeric_limits<std::uint64_t>::max())
        {
            return { JusticeDecision::InvalidSentence, found->second };
        }

        found->second.sentence = JailSentence{ mNextSentenceId++, days,
            ignoreTeleportation, ignoreSkillIncreases,
            std::move(progressText), std::move(endText) };
        return { JusticeDecision::Applied, found->second };
    }

    JusticeResult JusticeLedger::completeSentence(
        std::uint64_t player, std::uint64_t sentenceId, bool clearBounty)
    {
        if (player == 0)
            return { JusticeDecision::InvalidPlayer, {} };
        const auto found = mPlayers.find(player);
        if (found == mPlayers.end())
            return { JusticeDecision::InvalidPlayer, {} };
        if (!found->second.sentence)
            return { JusticeDecision::SentenceNotActive, found->second };
        if (sentenceId == 0 || sentenceId != found->second.sentence->id)
            return { JusticeDecision::StaleSentence, found->second };

        found->second.sentence.reset();
        if (clearBounty)
            found->second.bounty = 0;
        return { JusticeDecision::Applied, found->second };
    }

    std::optional<JusticeState> JusticeLedger::find(std::uint64_t player) const
    {
        const auto found = mPlayers.find(player);
        if (found == mPlayers.end())
            return std::nullopt;
        return found->second;
    }

    bool JusticeLedger::erase(std::uint64_t player) noexcept
    {
        return mPlayers.erase(player) != 0;
    }

    void JusticeLedger::clear() noexcept
    {
        mPlayers.clear();
    }

    std::size_t JusticeLedger::size() const noexcept
    {
        return mPlayers.size();
    }

    bool JusticeLedger::validBounty(std::int64_t bounty) noexcept
    {
        return bounty >= 0 && bounty <= MaximumBounty;
    }

    bool JusticeLedger::validText(const std::string& text) noexcept
    {
        if (text.size() > MaximumTextBytes)
            return false;
        return std::ranges::none_of(text, [](unsigned char value) {
            return value == 0;
        });
    }

    const char* describe(JusticeDecision decision) noexcept
    {
        switch (decision)
        {
            case JusticeDecision::Applied:
                return "the justice transition was applied";
            case JusticeDecision::InvalidPlayer:
                return "the justice transition has an invalid or unknown player";
            case JusticeDecision::InvalidBounty:
                return "the bounty is outside the canonical bounds";
            case JusticeDecision::UnauthorizedBountyReduction:
                return "only the server may reduce a bounty";
            case JusticeDecision::InvalidSentence:
                return "the jail sentence is invalid";
            case JusticeDecision::SentenceAlreadyActive:
                return "a jail sentence is already active";
            case JusticeDecision::SentenceNotActive:
                return "no jail sentence is active";
            case JusticeDecision::StaleSentence:
                return "the jail sentence ID is stale";
            case JusticeDecision::CapacityReached:
                return "the justice ledger is at capacity";
        }
        return "unknown justice decision";
    }
}
