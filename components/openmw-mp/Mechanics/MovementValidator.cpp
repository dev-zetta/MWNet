#include "MovementValidator.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace mwmp::mechanics
{
    MovementValidator::MovementValidator(std::size_t maximumConnections)
        : mMaximumConnections(maximumConnections)
    {
    }

    MovementValidationResult MovementValidator::validate(std::uint64_t connection,
        const MovementSample& sample, double theoreticalMaximumSpeed,
        Clock::time_point now)
    {
        if (connection == 0)
            return { MovementDecision::InvalidConnection };
        if (sample.sequence == 0)
            return { MovementDecision::InvalidSequence };
        if (!validPosition(sample.position) || !validCell(sample.cell))
            return { MovementDecision::InvalidCoordinate };
        if (!std::isfinite(theoreticalMaximumSpeed) || theoreticalMaximumSpeed < 0)
            return { MovementDecision::InvalidSpeed };

        const auto stateIt = mStates.find(connection);
        if (stateIt == mStates.end())
        {
            if (mStates.size() >= mMaximumConnections)
                return { MovementDecision::CapacityReached };

            const auto transitionIt = mTransitions.find(connection);
            if (transitionIt != mTransitions.end())
            {
                const Transition& transition = transitionIt->second;
                if (now <= transition.expiresAt && sample.cell == transition.cell
                    && (!transition.position
                        || distance(sample.position, *transition.position)
                            <= transition.tolerance))
                {
                    mStates.emplace(connection, State{ sample, now });
                    mTransitions.erase(transitionIt);
                    return { MovementDecision::AcceptedTransition };
                }
                if (now > transition.expiresAt)
                    mTransitions.erase(transitionIt);
                else
                    return { MovementDecision::TransitionNotAuthorized };
            }
            mStates.emplace(connection, State{ sample, now });
            return { MovementDecision::AcceptedInitial };
        }

        const State& previous = stateIt->second;
        if (sample.sequence <= previous.sample.sequence)
            return { MovementDecision::StaleSequence };

        const double travelled = distance(previous.sample.position, sample.position);
        const auto transitionIt = mTransitions.find(connection);
        if (transitionIt != mTransitions.end())
        {
            const Transition& transition = transitionIt->second;
            if (now <= transition.expiresAt && sample.cell == transition.cell
                && (!transition.position
                    || distance(sample.position, *transition.position)
                        <= transition.tolerance))
            {
                stateIt->second = { sample, now };
                mTransitions.erase(transitionIt);
                return { MovementDecision::AcceptedTransition, travelled, travelled };
            }
            if (now > transition.expiresAt)
                mTransitions.erase(transitionIt);
        }

        if (sample.cell != previous.sample.cell)
            return { MovementDecision::TransitionNotAuthorized, travelled, 0 };

        const auto elapsed = std::max(Clock::duration::zero(), now - previous.observedAt);
        const double seconds
            = std::chrono::duration<double>(elapsed + LatencyAllowance).count();
        const double allowed = DistanceMultiplier * theoreticalMaximumSpeed * seconds;
        if (travelled > allowed)
            return { MovementDecision::SpeedExceeded, travelled, allowed };

        stateIt->second = { sample, now };
        return { MovementDecision::Accepted, travelled, allowed };
    }

    MovementValidationResult MovementValidator::previewCellTransition(
        std::uint64_t connection, std::string_view destinationCell,
        Position3 previousPosition, double tolerance, Clock::time_point now,
        double theoreticalMaximumSpeed) const
    {
        if (connection == 0)
            return { MovementDecision::InvalidConnection };
        if (!validCell(destinationCell) || !validPosition(previousPosition)
            || !std::isfinite(tolerance) || tolerance < 0
            || !std::isfinite(theoreticalMaximumSpeed) || theoreticalMaximumSpeed < 0)
        {
            return { MovementDecision::InvalidTransition };
        }

        const auto state = mStates.find(connection);
        if (state == mStates.end())
        {
            if (mStates.size() >= mMaximumConnections)
                return { MovementDecision::CapacityReached };
            return { MovementDecision::AcceptedInitial };
        }
        if (destinationCell == state->second.sample.cell)
            return { MovementDecision::InvalidTransition };

        const double travelled
            = distance(state->second.sample.position, previousPosition);
        const auto elapsed = std::max(Clock::duration::zero(), now - state->second.observedAt);
        const double seconds = std::chrono::duration<double>(elapsed + LatencyAllowance).count();
        const double allowed = tolerance + DistanceMultiplier * theoreticalMaximumSpeed * seconds;
        if (travelled > allowed)
            return { MovementDecision::SpeedExceeded, travelled, allowed };
        return { MovementDecision::AcceptedTransition, travelled, allowed };
    }

    MovementValidationResult MovementValidator::acceptCellTransition(
        std::uint64_t connection, std::string destinationCell,
        Position3 previousPosition, double tolerance, Clock::time_point now,
        std::chrono::milliseconds lifetime, double theoreticalMaximumSpeed)
    {
        const MovementValidationResult result = previewCellTransition(connection,
            destinationCell, previousPosition, tolerance, now, theoreticalMaximumSpeed);
        if (!result.accepted() || lifetime <= lifetime.zero())
            return result.accepted()
                ? MovementValidationResult{ MovementDecision::InvalidTransition }
                : result;

        const auto existing = mTransitions.find(connection);
        if (existing != mTransitions.end() && now <= existing->second.expiresAt
            && existing->second.cell == destinationCell
            && existing->second.position)
        {
            return result;
        }
        mTransitions.insert_or_assign(connection,
            Transition{ std::move(destinationCell), std::nullopt, 0,
                now + lifetime });
        return result;
    }

    MovementValidationResult MovementValidator::previewAuthorizedTransition(
        std::uint64_t connection, const MovementSample& destination,
        Clock::time_point now) const
    {
        if (connection == 0)
            return { MovementDecision::InvalidConnection };
        if (destination.sequence == 0)
            return { MovementDecision::InvalidSequence };
        if (!validPosition(destination.position) || !validCell(destination.cell))
            return { MovementDecision::InvalidCoordinate };
        if (const auto state = mStates.find(connection); state != mStates.end()
            && destination.sequence <= state->second.sample.sequence)
            return { MovementDecision::StaleSequence };

        const auto found = mTransitions.find(connection);
        if (found == mTransitions.end() || now > found->second.expiresAt
            || found->second.cell != destination.cell || !found->second.position)
            return { MovementDecision::TransitionNotAuthorized };
        const double travelled = distance(destination.position, *found->second.position);
        if (travelled > found->second.tolerance)
            return { MovementDecision::TransitionNotAuthorized, travelled, found->second.tolerance };
        return { MovementDecision::AcceptedTransition, travelled, found->second.tolerance };
    }

    MovementValidationResult MovementValidator::commitCellTransition(
        std::uint64_t connection, const MovementSample& destination,
        Position3 previousPosition, double tolerance, double theoreticalMaximumSpeed,
        Clock::time_point now)
    {
        if (!mStates.contains(connection) && mStates.size() >= mMaximumConnections)
            return { MovementDecision::CapacityReached };
        // Stage only this connection. A rejected destination must not leave an
        // authorization behind or change the previously accepted movement baseline.
        MovementValidator staged(1);
        if (const auto state = mStates.find(connection); state != mStates.end())
            staged.mStates.emplace(connection, state->second);
        if (const auto transition = mTransitions.find(connection); transition != mTransitions.end())
            staged.mTransitions.emplace(connection, transition->second);
        auto result = staged.previewAuthorizedTransition(connection, destination, now);
        if (!result.accepted())
            result = staged.acceptCellTransition(connection, destination.cell,
                previousPosition, tolerance, now, TransitionLifetime, theoreticalMaximumSpeed);
        if (!result.accepted())
            return result;
        result = staged.validate(connection, destination, theoreticalMaximumSpeed, now);
        if (!result.accepted())
            return result;
        mStates.insert_or_assign(connection, staged.mStates.at(connection));
        mTransitions.erase(connection);
        return result;
    }

    bool MovementValidator::authorizeTransition(std::uint64_t connection, std::string cell,
        Position3 position, double tolerance, Clock::time_point now,
        std::chrono::milliseconds lifetime)
    {
        if (connection == 0 || !validCell(cell) || !validPosition(position)
            || !std::isfinite(tolerance) || tolerance < 0 || lifetime <= lifetime.zero())
            return false;
        if (!mStates.contains(connection) && mStates.size() >= mMaximumConnections)
            return false;
        mTransitions.insert_or_assign(connection,
            Transition{ std::move(cell), std::optional<Position3>{ position },
                tolerance, now + lifetime });
        return true;
    }

    bool MovementValidator::cancelTransition(std::uint64_t connection) noexcept
    {
        return mTransitions.erase(connection) != 0;
    }

    bool MovementValidator::erase(std::uint64_t connection) noexcept
    {
        mTransitions.erase(connection);
        return mStates.erase(connection) != 0;
    }

    void MovementValidator::clear() noexcept
    {
        mTransitions.clear();
        mStates.clear();
    }

    std::optional<MovementSample> MovementValidator::current(std::uint64_t connection) const
    {
        const auto found = mStates.find(connection);
        if (found == mStates.end())
            return std::nullopt;
        return found->second.sample;
    }

    std::size_t MovementValidator::size() const noexcept
    {
        return mStates.size();
    }

    bool MovementValidator::validPosition(const Position3& position) noexcept
    {
        const auto valid = [](double value) {
            return std::isfinite(value) && std::abs(value) <= MaximumCoordinateMagnitude;
        };
        return valid(position.x) && valid(position.y) && valid(position.z);
    }

    bool MovementValidator::validCell(std::string_view cell) noexcept
    {
        return !cell.empty() && cell.size() <= MaximumCellBytes;
    }

    double MovementValidator::distance(const Position3& left, const Position3& right) noexcept
    {
        return std::hypot(left.x - right.x, left.y - right.y, left.z - right.z);
    }

    const char* describe(MovementDecision decision) noexcept
    {
        switch (decision)
        {
            case MovementDecision::AcceptedInitial:
                return "initial movement baseline accepted";
            case MovementDecision::Accepted:
                return "movement accepted";
            case MovementDecision::AcceptedTransition:
                return "authorized transition accepted";
            case MovementDecision::InvalidConnection:
                return "the connection identifier is invalid";
            case MovementDecision::InvalidSequence:
                return "the movement sequence is invalid";
            case MovementDecision::StaleSequence:
                return "the movement sequence is stale";
            case MovementDecision::InvalidCoordinate:
                return "the movement coordinate or cell is invalid";
            case MovementDecision::InvalidSpeed:
                return "the theoretical speed is invalid";
            case MovementDecision::SpeedExceeded:
                return "the movement exceeds the theoretical speed bound";
            case MovementDecision::InvalidTransition:
                return "the cell transition intent is invalid";
            case MovementDecision::TransitionNotAuthorized:
                return "the cell transition was not authorized";
            case MovementDecision::CapacityReached:
                return "the movement validator is at capacity";
        }
        return "unknown movement validation result";
    }
}
