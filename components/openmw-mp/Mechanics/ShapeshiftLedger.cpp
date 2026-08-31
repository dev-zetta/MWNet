#include "ShapeshiftLedger.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace mwmp::mechanics
{
    ShapeshiftLedger::ShapeshiftLedger(std::size_t maximumPlayers)
        : mMaximumPlayers(maximumPlayers)
    {
    }

    ShapeshiftResult ShapeshiftLedger::set(
        std::uint64_t player, ShapeshiftState state)
    {
        if (player == 0)
            return { ShapeshiftDecision::InvalidPlayer, {} };
        const ShapeshiftDecision decision = validate(state);
        if (decision != ShapeshiftDecision::Applied)
            return { decision, {} };

        auto found = mPlayers.find(player);
        if (found == mPlayers.end())
        {
            if (mPlayers.size() >= mMaximumPlayers)
                return { ShapeshiftDecision::CapacityReached, {} };
            found = mPlayers.emplace(player, std::move(state)).first;
        }
        else
            found->second = std::move(state);
        return { ShapeshiftDecision::Applied, found->second };
    }

    ShapeshiftResult ShapeshiftLedger::previewClientIntent(
        std::uint64_t player, const ShapeshiftState& state) const
    {
        if (player == 0)
            return { ShapeshiftDecision::InvalidPlayer, {} };
        const ShapeshiftDecision decision = validate(state);
        if (decision != ShapeshiftDecision::Applied)
            return { decision, {} };

        const auto found = mPlayers.find(player);
        if (found == mPlayers.end())
            return { ShapeshiftDecision::UnknownPlayer, {} };

        const ShapeshiftState& current = found->second;
        if (state.scale != current.scale
            || state.displayCreatureName != current.displayCreatureName
            || state.creatureRefId != current.creatureRefId)
        {
            return { ShapeshiftDecision::UnauthorizedAppearanceChange, current };
        }
        return { ShapeshiftDecision::Applied, state };
    }

    ShapeshiftResult ShapeshiftLedger::applyClientIntent(
        std::uint64_t player, const ShapeshiftState& state)
    {
        ShapeshiftResult result = previewClientIntent(player, state);
        if (result.applied())
            mPlayers.find(player)->second = result.state;
        return result;
    }

    std::optional<ShapeshiftState> ShapeshiftLedger::find(
        std::uint64_t player) const
    {
        const auto found = mPlayers.find(player);
        if (found == mPlayers.end())
            return std::nullopt;
        return found->second;
    }

    bool ShapeshiftLedger::erase(std::uint64_t player) noexcept
    {
        return mPlayers.erase(player) != 0;
    }

    void ShapeshiftLedger::clear() noexcept
    {
        mPlayers.clear();
    }

    std::size_t ShapeshiftLedger::size() const noexcept
    {
        return mPlayers.size();
    }

    ShapeshiftDecision ShapeshiftLedger::validate(
        const ShapeshiftState& state) noexcept
    {
        if (!std::isfinite(state.scale) || state.scale < MinimumScale
            || state.scale > MaximumScale)
        {
            return ShapeshiftDecision::InvalidScale;
        }
        if (state.creatureRefId.size() > MaximumCreatureRefIdBytes
            || std::ranges::any_of(state.creatureRefId, [](unsigned char value) {
                return value == 0;
            }))
        {
            return ShapeshiftDecision::InvalidCreatureRefId;
        }
        return ShapeshiftDecision::Applied;
    }

    const char* describe(ShapeshiftDecision decision) noexcept
    {
        switch (decision)
        {
            case ShapeshiftDecision::Applied:
                return "the shapeshift state was applied";
            case ShapeshiftDecision::InvalidPlayer:
                return "the shapeshift state has an invalid player";
            case ShapeshiftDecision::InvalidScale:
                return "the player scale is outside the canonical bounds";
            case ShapeshiftDecision::InvalidCreatureRefId:
                return "the creature disguise identifier is invalid";
            case ShapeshiftDecision::UnauthorizedAppearanceChange:
                return "only the server may change player scale or disguise";
            case ShapeshiftDecision::UnknownPlayer:
                return "the player has no canonical shapeshift state";
            case ShapeshiftDecision::CapacityReached:
                return "the shapeshift ledger is at capacity";
        }
        return "unknown shapeshift decision";
    }
}
