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

    ActorStateResult ActorStateLedger::previewCellChanges(
        const std::vector<ActorCellChangeUpdate>& updates) const
    {
        return validateCellChanges(updates);
    }

    ActorStateResult ActorStateLedger::applyCellChanges(
        const std::vector<ActorCellChangeUpdate>& updates,
        Clock::time_point now)
    {
        const ActorStateResult result = validateCellChanges(updates);
        if (!result.applied())
            return result;

        std::vector<ActorIdentity> destinations;
        destinations.reserve(updates.size());
        for (const ActorCellChangeUpdate& update : updates)
        {
            destinations.push_back({ update.destinationCell,
                update.source.refNum, update.source.mpNum });
        }

        mActors.reserve(mActors.size() + updates.size());
        try
        {
            for (std::size_t index = 0; index < updates.size(); ++index)
            {
                ActorState state = mActors.at(updates[index].source);
                state.movement = ActorMovementState{ updates[index].transform,
                    updates[index].sequence, now };
                mActors.emplace(destinations[index], std::move(state));
            }
        }
        catch (...)
        {
            for (const ActorIdentity& destination : destinations)
                mActors.erase(destination);
            throw;
        }

        for (const ActorCellChangeUpdate& update : updates)
            mActors.erase(update.source);
        return { ActorStateDecision::Applied, mActors.size() };
    }

    ActorStateResult ActorStateLedger::previewRoster(ActorRosterAction action,
        const std::string& cell,
        const std::vector<ActorRosterUpdate>& updates) const
    {
        return validateRoster(action, cell, updates);
    }

    ActorStateResult ActorStateLedger::applyRoster(ActorRosterAction action,
        const std::string& cell,
        const std::vector<ActorRosterUpdate>& updates)
    {
        const ActorStateResult result = validateRoster(action, cell, updates);
        if (!result.applied())
            return result;

        if (action == ActorRosterAction::Set)
        {
            std::unordered_set<ActorIdentity, ActorIdentityHash> replacement;
            replacement.reserve(updates.size());
            for (const ActorRosterUpdate& update : updates)
                replacement.insert(update.identity);
            for (auto actor = mActors.begin(); actor != mActors.end();)
            {
                if (actor->first.cell == cell && !replacement.contains(actor->first))
                    actor = mActors.erase(actor);
                else
                    ++actor;
            }
        }

        if (action == ActorRosterAction::Remove)
        {
            for (const ActorRosterUpdate& update : updates)
                mActors.erase(update.identity);
        }
        else
        {
            for (const ActorRosterUpdate& update : updates)
                mActors.try_emplace(update.identity).first->second.refId = update.refId;
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

    std::optional<std::string> ActorStateLedger::refId(
        const ActorIdentity& identity) const
    {
        const auto found = mActors.find(identity);
        if (found == mActors.end() || found->second.refId.empty())
            return std::nullopt;
        return found->second.refId;
    }

    bool ActorStateLedger::contains(const ActorIdentity& identity) const noexcept
    {
        return mActors.contains(identity);
    }

    std::vector<ActorIdentity> ActorStateLedger::identities(
        const std::string& cell) const
    {
        std::vector<ActorIdentity> result;
        for (const auto& actor : mActors)
        {
            if (actor.first.cell == cell)
                result.push_back(actor.first);
        }
        return result;
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

    void ActorStateLedger::swap(ActorStateLedger& other) noexcept
    {
        std::swap(mMaximumActors, other.mMaximumActors);
        mActors.swap(other.mActors);
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

    ActorStateResult ActorStateLedger::validateRoster(ActorRosterAction action,
        const std::string& cell,
        const std::vector<ActorRosterUpdate>& updates) const
    {
        if (cell.empty() || cell.size() > MaximumCellBytes)
            return { ActorStateDecision::InvalidIdentity, mActors.size() };
        if (updates.size() > MaximumChanges
            || (updates.empty() && action != ActorRosterAction::Set))
            return { ActorStateDecision::InvalidBatch, mActors.size() };

        std::unordered_set<ActorIdentity, ActorIdentityHash> identities;
        std::size_t newActors = 0;
        for (const ActorRosterUpdate& update : updates)
        {
            if (!validIdentity(update.identity) || update.identity.cell != cell)
                return { ActorStateDecision::InvalidIdentity, mActors.size() };
            if (!identities.insert(update.identity).second)
                return { ActorStateDecision::DuplicateActor, mActors.size() };
            if (action != ActorRosterAction::Remove
                && (update.refId.empty() || update.refId.size() > MaximumRefIdBytes))
                return { ActorStateDecision::InvalidRefId, mActors.size() };
            if (action == ActorRosterAction::Remove)
            {
                if (!mActors.contains(update.identity))
                    return { ActorStateDecision::UnknownActor, mActors.size() };
            }
            else if (!mActors.contains(update.identity))
                ++newActors;
        }

        std::size_t resultingSize = mActors.size();
        if (action == ActorRosterAction::Set)
        {
            const std::size_t currentCellActors = std::count_if(mActors.begin(),
                mActors.end(), [&cell](const auto& actor) {
                    return actor.first.cell == cell;
                });
            resultingSize -= currentCellActors;
            resultingSize += updates.size();
        }
        else if (action == ActorRosterAction::Add)
            resultingSize += newActors;
        else
            resultingSize -= updates.size();

        if (resultingSize > mMaximumActors)
            return { ActorStateDecision::ActorLimitReached, mActors.size() };
        return { ActorStateDecision::Applied, resultingSize };
    }

    ActorStateResult ActorStateLedger::validateCellChanges(
        const std::vector<ActorCellChangeUpdate>& updates) const
    {
        if (updates.empty() || updates.size() > MaximumChanges)
            return { ActorStateDecision::InvalidBatch, mActors.size() };

        std::unordered_set<ActorIdentity, ActorIdentityHash> sources;
        std::unordered_set<ActorIdentity, ActorIdentityHash> destinations;
        for (const ActorCellChangeUpdate& update : updates)
        {
            if (!validIdentity(update.source)
                || update.destinationCell.empty()
                || update.destinationCell.size() > MaximumCellBytes
                || update.destinationCell == update.source.cell)
            {
                return { ActorStateDecision::InvalidIdentity, mActors.size() };
            }
            if (!sources.insert(update.source).second)
                return { ActorStateDecision::DuplicateActor, mActors.size() };
            if (!validTransform(update.transform))
                return { ActorStateDecision::InvalidPosition, mActors.size() };
            if (update.sequence == 0)
                return { ActorStateDecision::InvalidSequence, mActors.size() };
            if (!mActors.contains(update.source))
                return { ActorStateDecision::UnknownActor, mActors.size() };
        }

        for (const ActorCellChangeUpdate& update : updates)
        {
            const ActorIdentity destination{ update.destinationCell,
                update.source.refNum, update.source.mpNum };
            if (!destinations.insert(destination).second)
                return { ActorStateDecision::DuplicateActor, mActors.size() };
            if (mActors.contains(destination))
                return { ActorStateDecision::DestinationOccupied, mActors.size() };
        }
        return { ActorStateDecision::Applied, mActors.size() };
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
            case ActorStateDecision::InvalidRosterAction:
                return "the actor roster action is invalid";
            case ActorStateDecision::InvalidRefId:
                return "an actor record identifier is invalid";
            case ActorStateDecision::UnknownActor:
                return "the actor is not present in canonical state";
            case ActorStateDecision::DestinationOccupied:
                return "the actor destination is already occupied";
            case ActorStateDecision::ActorLimitReached:
                return "the canonical actor limit was reached";
        }
        return "unknown actor state decision";
    }
}
