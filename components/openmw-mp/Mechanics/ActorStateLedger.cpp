#include "ActorStateLedger.hpp"

#include <algorithm>
#include <cmath>
#include <functional>
#include <unordered_set>
#include <utility>

namespace mwmp::mechanics
{
    std::size_t ActorIdentityHash::operator()(const ActorIdentity& identity) const noexcept
    {
        std::size_t seed = std::hash<std::string>{}(identity.cell);
        seed ^= std::hash<std::uint32_t>{}(identity.refNum) + 0x9e3779b9
            + (seed << 6) + (seed >> 2);
        seed ^= std::hash<std::uint32_t>{}(identity.mpNum) + 0x9e3779b9
            + (seed << 6) + (seed >> 2);
        return seed;
    }

    ActorStateLedger::ActorStateLedger(std::size_t maximumActors)
        : mMaximumActors(maximumActors)
    {
    }

    ActorStateResult ActorStateLedger::previewEquipment(
        const std::vector<ActorEquipmentUpdate>& updates) const
    {
        return validate(updates);
    }

    ActorStateResult ActorStateLedger::applyEquipment(
        const std::vector<ActorEquipmentUpdate>& updates)
    {
        const ActorStateResult result = validate(updates);
        if (!result.applied())
            return result;
        for (const ActorEquipmentUpdate& update : updates)
            mActors.try_emplace(update.identity).first->second.equipment = update.equipment;
        return { ActorStateDecision::Applied, mActors.size() };
    }

    ActorStateResult ActorStateLedger::previewPositions(
        const std::vector<ActorPositionUpdate>& updates,
        double theoreticalMaximumSpeed, Clock::time_point now) const
    {
        return validatePositions(updates, theoreticalMaximumSpeed, now);
    }

    ActorStateResult ActorStateLedger::applyPositions(
        const std::vector<ActorPositionUpdate>& updates,
        double theoreticalMaximumSpeed, Clock::time_point now)
    {
        const ActorStateResult result
            = validatePositions(updates, theoreticalMaximumSpeed, now);
        if (!result.applied())
            return result;
        for (const ActorPositionUpdate& update : updates)
        {
            ActorState& actor = mActors.try_emplace(update.identity).first->second;
            actor.movement = ActorMovementState{ update.transform, update.sequence, now };
        }
        return { ActorStateDecision::Applied, mActors.size() };
    }

    std::optional<EquipmentLedger::Equipment> ActorStateLedger::equipment(
        const ActorIdentity& identity) const
    {
        const auto found = mActors.find(identity);
        if (found == mActors.end() || !found->second.equipment)
            return std::nullopt;
        return found->second.equipment;
    }

    std::optional<ActorTransform> ActorStateLedger::position(
        const ActorIdentity& identity) const
    {
        const auto found = mActors.find(identity);
        if (found == mActors.end() || !found->second.movement)
            return std::nullopt;
        return found->second.movement->transform;
    }

    std::size_t ActorStateLedger::eraseCell(const std::string& cell) noexcept
    {
        std::size_t erased = 0;
        for (auto actor = mActors.begin(); actor != mActors.end();)
        {
            if (actor->first.cell == cell)
            {
                actor = mActors.erase(actor);
                ++erased;
            }
            else
                ++actor;
        }
        return erased;
    }

    void ActorStateLedger::clear() noexcept
    {
        mActors.clear();
    }

    std::size_t ActorStateLedger::size() const noexcept
    {
        return mActors.size();
    }

    ActorStateResult ActorStateLedger::validate(
        const std::vector<ActorEquipmentUpdate>& updates) const
    {
        if (updates.empty() || updates.size() > MaximumChanges)
            return { ActorStateDecision::InvalidBatch, mActors.size() };

        std::unordered_set<ActorIdentity, ActorIdentityHash> identities;
        std::size_t newActors = 0;
        for (const ActorEquipmentUpdate& update : updates)
        {
            if (!validIdentity(update.identity))
                return { ActorStateDecision::InvalidIdentity, mActors.size() };
            if (!identities.insert(update.identity).second)
                return { ActorStateDecision::DuplicateActor, mActors.size() };
            if (!validEquipment(update.equipment))
                return { ActorStateDecision::InvalidEquipment, mActors.size() };
            if (!mActors.contains(update.identity))
                ++newActors;
        }
        if (newActors > mMaximumActors - std::min(mMaximumActors, mActors.size()))
            return { ActorStateDecision::ActorLimitReached, mActors.size() };
        return { ActorStateDecision::Applied, mActors.size() + newActors };
    }

    ActorStateResult ActorStateLedger::validatePositions(
        const std::vector<ActorPositionUpdate>& updates,
        double theoreticalMaximumSpeed, Clock::time_point now) const
    {
        if (updates.empty() || updates.size() > MaximumChanges)
            return { ActorStateDecision::InvalidBatch, mActors.size() };
        if (!std::isfinite(theoreticalMaximumSpeed) || theoreticalMaximumSpeed < 0)
            return { ActorStateDecision::InvalidSpeed, mActors.size() };

        std::unordered_set<ActorIdentity, ActorIdentityHash> identities;
        std::size_t newActors = 0;
        for (const ActorPositionUpdate& update : updates)
        {
            if (!validIdentity(update.identity))
                return { ActorStateDecision::InvalidIdentity, mActors.size() };
            if (!identities.insert(update.identity).second)
                return { ActorStateDecision::DuplicateActor, mActors.size() };
            if (!validTransform(update.transform))
                return { ActorStateDecision::InvalidPosition, mActors.size() };
            if (update.sequence == 0)
                return { ActorStateDecision::InvalidSequence, mActors.size() };

            const auto actor = mActors.find(update.identity);
            if (actor == mActors.end())
            {
                ++newActors;
                continue;
            }
            if (!actor->second.movement)
                continue;

            const ActorMovementState& previous = *actor->second.movement;
            if (update.sequence <= previous.sequence)
                return { ActorStateDecision::StaleSequence, mActors.size() };
            const auto elapsed = std::max(Clock::duration::zero(), now - previous.observedAt);
            const double seconds
                = std::chrono::duration<double>(elapsed + LatencyAllowance).count();
            const double allowed
                = DistanceMultiplier * theoreticalMaximumSpeed * seconds;
            const double travelled
                = distance(previous.transform.position, update.transform.position);
            if (travelled > allowed)
                return { ActorStateDecision::SpeedExceeded, mActors.size(),
                    travelled, allowed };
        }
        if (newActors > mMaximumActors - std::min(mMaximumActors, mActors.size()))
            return { ActorStateDecision::ActorLimitReached, mActors.size() };
        return { ActorStateDecision::Applied, mActors.size() + newActors };
    }

    bool ActorStateLedger::validIdentity(const ActorIdentity& identity) noexcept
    {
        return !identity.cell.empty() && identity.cell.size() <= MaximumCellBytes
            && (identity.refNum != 0 || identity.mpNum != 0);
    }

    bool ActorStateLedger::validEquipment(
        const EquipmentLedger::Equipment& equipment) noexcept
    {
        for (const EquipmentItem& item : equipment)
        {
            if (item.empty())
            {
                if (item.count != 0)
                    return false;
                continue;
            }
            if (item.refId.size() > InventoryLedger::MaximumStringBytes
                || item.count <= 0 || item.count > InventoryLedger::MaximumStackCount
                || item.charge < -1 || !std::isfinite(item.enchantmentCharge)
                || item.enchantmentCharge < -1
                || item.enchantmentCharge > InventoryLedger::MaximumEnchantmentCharge)
            {
                return false;
            }
        }
        return true;
    }

    bool ActorStateLedger::validTransform(const ActorTransform& transform) noexcept
    {
        const auto validPosition = [](const Position3& value) {
            const auto validCoordinate = [](double coordinate) {
                return std::isfinite(coordinate)
                    && std::abs(coordinate) <= MovementValidator::MaximumCoordinateMagnitude;
            };
            return validCoordinate(value.x) && validCoordinate(value.y)
                && validCoordinate(value.z);
        };
        return validPosition(transform.position) && validPosition(transform.rotation)
            && validPosition(transform.direction)
            && validPosition(transform.directionRotation);
    }

    double ActorStateLedger::distance(
        const Position3& left, const Position3& right) noexcept
    {
        return std::hypot(left.x - right.x, left.y - right.y, left.z - right.z);
    }

    const char* describe(ActorStateDecision decision) noexcept
    {
        switch (decision)
        {
            case ActorStateDecision::Applied:
                return "the actor state update was applied";
            case ActorStateDecision::InvalidBatch:
                return "the actor state batch is invalid";
            case ActorStateDecision::InvalidIdentity:
                return "an actor identity is invalid";
            case ActorStateDecision::DuplicateActor:
                return "an actor occurs more than once in the batch";
            case ActorStateDecision::InvalidEquipment:
                return "an actor equipment item is invalid";
            case ActorStateDecision::InvalidPosition:
                return "an actor transform is invalid";
            case ActorStateDecision::InvalidSpeed:
                return "the actor speed bound is invalid";
            case ActorStateDecision::InvalidSequence:
                return "the actor movement sequence is invalid";
            case ActorStateDecision::StaleSequence:
                return "the actor movement sequence is stale";
            case ActorStateDecision::SpeedExceeded:
                return "the actor movement exceeds the theoretical speed bound";
            case ActorStateDecision::ActorLimitReached:
                return "the canonical actor limit was reached";
        }
        return "unknown actor state decision";
    }
}
