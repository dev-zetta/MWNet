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
                && distance(sample.position, transition.position) <= transition.tolerance)
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
            Transition{ std::move(cell), position, tolerance, now + lifetime });
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
            case MovementDecision::TransitionNotAuthorized:
                return "the cell transition was not authorized";
            case MovementDecision::CapacityReached:
                return "the movement validator is at capacity";
        }
        return "unknown movement validation result";
    }
}
