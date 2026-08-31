#include "ObjectStateLedger.hpp"

#include <algorithm>
#include <cmath>
#include <functional>
#include <utility>

namespace mwmp::mechanics
{
    std::size_t ObjectIdentityHash::operator()(const ObjectIdentity& identity) const noexcept
    {
        std::size_t seed = std::hash<std::string>{}(identity.cell);
        seed ^= std::hash<std::uint32_t>{}(identity.refNum) + 0x9e3779b9
            + (seed << 6) + (seed >> 2);
        seed ^= std::hash<std::uint32_t>{}(identity.mpNum) + 0x9e3779b9
            + (seed << 6) + (seed >> 2);
        return seed;
    }

    ObjectStateLedger::ObjectStateLedger(
        std::size_t maximumObjects, std::size_t maximumDynamicObjectsPerPlayer)
        : mMaximumObjects(maximumObjects)
        , mMaximumDynamicObjectsPerPlayer(maximumDynamicObjectsPerPlayer)
    {
    }

    ObjectResult ObjectStateLedger::previewBatch(
        const std::vector<ObjectMutation>& mutations) const
    {
        return prepare(mutations).result;
    }

    ObjectResult ObjectStateLedger::applyBatch(
        const std::vector<ObjectMutation>& mutations)
    {
        PreparedBatch prepared = prepare(mutations);
        if (!prepared.result.applied())
            return prepared.result;

        for (auto& [identity, object] : prepared.candidates)
            mObjects.insert_or_assign(std::move(identity), std::move(object));
        for (const auto& [creator, count] : prepared.creatorAdditions)
            mDynamicObjectsByCreator[creator] += count;
        return { ObjectDecision::Applied, mObjects.size() };
    }

    std::optional<ObjectState> ObjectStateLedger::find(
        const ObjectIdentity& identity) const
    {
        const auto found = mObjects.find(identity);
        if (found == mObjects.end())
            return std::nullopt;
        return found->second;
    }

    std::size_t ObjectStateLedger::dynamicObjectCount(
        std::uint64_t creator) const noexcept
    {
        const auto found = mDynamicObjectsByCreator.find(creator);
        return found == mDynamicObjectsByCreator.end() ? 0 : found->second;
    }

    std::size_t ObjectStateLedger::size() const noexcept
    {
        return mObjects.size();
    }

    void ObjectStateLedger::clear() noexcept
    {
        mObjects.clear();
        mDynamicObjectsByCreator.clear();
    }

    bool ObjectStateLedger::validIdentity(const ObjectIdentity& identity) noexcept
    {
        return validText(identity.cell, MaximumCellBytes)
            && !identity.cell.empty()
            && (identity.refNum != 0 || identity.mpNum != 0);
    }

    bool ObjectStateLedger::validObject(const ObjectState& object) noexcept
    {
        return validIdentity(object.identity)
            && validText(object.refId, MaximumStringBytes) && !object.refId.empty()
            && validText(object.soul, MaximumStringBytes)
            && object.count > 0 && object.count <= MaximumCount
            && object.charge >= -1 && object.charge <= MaximumCount
            && std::isfinite(object.enchantmentCharge)
            && object.enchantmentCharge >= -1.0
            && object.enchantmentCharge <= MaximumCharge
            && object.goldValue >= 0 && object.goldValue <= MaximumCount
            && validVector(object.position) && validVector(object.rotation)
            && std::isfinite(object.scale) && object.scale > 0.0
            && object.scale <= MaximumScale
            && object.lockLevel >= -1 && object.lockLevel <= MaximumLockLevel
            && object.doorState >= 0 && object.doorState <= MaximumDoorState;
    }

    bool ObjectStateLedger::validVector(
        const std::array<double, 3>& values) noexcept
    {
        return std::ranges::all_of(values, [](double value) {
            return std::isfinite(value) && std::abs(value) <= MaximumCoordinate;
        });
    }

    bool ObjectStateLedger::validText(
        const std::string& value, std::size_t limit) noexcept
    {
        return value.size() <= limit
            && std::ranges::none_of(value, [](unsigned char character) {
                return character == 0;
            });
    }

    ObjectStateLedger::PreparedBatch ObjectStateLedger::prepare(
        const std::vector<ObjectMutation>& mutations) const
    {
        PreparedBatch prepared;
        if (mutations.size() > MaximumMutations)
        {
            prepared.result = { ObjectDecision::MutationLimitReached, mObjects.size() };
            return prepared;
        }

        std::size_t newObjects = 0;
        for (const ObjectMutation& mutation : mutations)
        {
            if (!validIdentity(mutation.object.identity))
            {
                prepared.result = { ObjectDecision::InvalidIdentity, mObjects.size() };
                return prepared;
            }

            auto candidate = prepared.candidates.find(mutation.object.identity);
            if (candidate == prepared.candidates.end())
            {
                const auto existing = mObjects.find(mutation.object.identity);
                if (existing != mObjects.end())
                    candidate = prepared.candidates.emplace(
                        mutation.object.identity, existing->second).first;
            }

            if (mutation.kind == ObjectMutationKind::Seed
                || mutation.kind == ObjectMutationKind::Place)
            {
                if (!validObject(mutation.object))
                {
                    prepared.result = { ObjectDecision::InvalidObject, mObjects.size() };
                    return prepared;
                }
                if (mutation.kind == ObjectMutationKind::Place
                    && (mutation.object.creator == 0 || mutation.object.identity.mpNum == 0))
                {
                    prepared.result = { ObjectDecision::InvalidObject, mObjects.size() };
                    return prepared;
                }
                if (candidate != prepared.candidates.end())
                {
                    prepared.result = { ObjectDecision::DuplicateObject, mObjects.size() };
                    return prepared;
                }
                if (mObjects.size() + newObjects >= mMaximumObjects)
                {
                    prepared.result = { ObjectDecision::ObjectLimitReached, mObjects.size() };
                    return prepared;
                }
                if (mutation.object.creator != 0)
                {
                    const std::size_t existingCount = dynamicObjectCount(
                        mutation.object.creator);
                    std::size_t& added = prepared.creatorAdditions[
                        mutation.object.creator];
                    if (existingCount + added >= mMaximumDynamicObjectsPerPlayer)
                    {
                        prepared.result = { ObjectDecision::PlayerLimitReached, mObjects.size() };
                        return prepared;
                    }
                    ++added;
                }
                prepared.candidates.emplace(
                    mutation.object.identity, mutation.object);
                ++newObjects;
                continue;
            }

            if (candidate == prepared.candidates.end())
            {
                prepared.result = { ObjectDecision::MissingObject, mObjects.size() };
                return prepared;
            }
            ObjectState& object = candidate->second;
            if (object.deleted)
            {
                prepared.result = { ObjectDecision::DeletedObject, mObjects.size() };
                return prepared;
            }

            switch (mutation.kind)
            {
                case ObjectMutationKind::SetEnabled:
                    object.enabled = mutation.object.enabled;
                    break;
                case ObjectMutationKind::Move:
                    if (!validVector(mutation.object.position))
                    {
                        prepared.result = { ObjectDecision::InvalidObject, mObjects.size() };
                        return prepared;
                    }
                    object.position = mutation.object.position;
                    break;
                case ObjectMutationKind::Rotate:
                    if (!validVector(mutation.object.rotation))
                    {
                        prepared.result = { ObjectDecision::InvalidObject, mObjects.size() };
                        return prepared;
                    }
                    object.rotation = mutation.object.rotation;
                    break;
                case ObjectMutationKind::Scale:
                    if (!std::isfinite(mutation.object.scale)
                        || mutation.object.scale <= 0.0
                        || mutation.object.scale > MaximumScale)
                    {
                        prepared.result = { ObjectDecision::InvalidObject, mObjects.size() };
                        return prepared;
                    }
                    object.scale = mutation.object.scale;
                    break;
                case ObjectMutationKind::SetLock:
                    if (mutation.object.lockLevel < -1
                        || mutation.object.lockLevel > MaximumLockLevel)
                    {
                        prepared.result = { ObjectDecision::InvalidObject, mObjects.size() };
                        return prepared;
                    }
                    object.lockLevel = mutation.object.lockLevel;
                    break;
                case ObjectMutationKind::SetDoorState:
                    if (mutation.object.doorState < 0
                        || mutation.object.doorState > MaximumDoorState)
                    {
                        prepared.result = { ObjectDecision::InvalidObject,
                            mObjects.size() };
                        return prepared;
                    }
                    object.doorState = mutation.object.doorState;
                    break;
                case ObjectMutationKind::Delete:
                    object.deleted = true;
                    break;
                case ObjectMutationKind::Seed:
                case ObjectMutationKind::Place:
                    break;
                default:
                    prepared.result = { ObjectDecision::InvalidMutation, mObjects.size() };
                    return prepared;
            }
        }

        prepared.result = { ObjectDecision::Applied, mObjects.size() + newObjects };
        return prepared;
    }

    const char* describe(ObjectDecision decision) noexcept
    {
        switch (decision)
        {
            case ObjectDecision::Applied:
                return "the object mutation was applied";
            case ObjectDecision::InvalidIdentity:
                return "the object identity is invalid";
            case ObjectDecision::InvalidMutation:
                return "the object mutation type is invalid";
            case ObjectDecision::InvalidObject:
                return "the object state is invalid";
            case ObjectDecision::MissingObject:
                return "the object does not exist in canonical state";
            case ObjectDecision::DeletedObject:
                return "the object has already been deleted";
            case ObjectDecision::DuplicateObject:
                return "the object identity already exists";
            case ObjectDecision::ObjectLimitReached:
                return "the world object quota was reached";
            case ObjectDecision::PlayerLimitReached:
                return "the per-player object quota was reached";
            case ObjectDecision::MutationLimitReached:
                return "the object mutation batch is too large";
        }
        return "unknown object decision";
    }
}
