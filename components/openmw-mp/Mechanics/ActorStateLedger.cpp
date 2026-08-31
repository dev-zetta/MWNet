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
            mEquipment.insert_or_assign(update.identity, update.equipment);
        return { ActorStateDecision::Applied, mEquipment.size() };
    }

    std::optional<EquipmentLedger::Equipment> ActorStateLedger::equipment(
        const ActorIdentity& identity) const
    {
        const auto found = mEquipment.find(identity);
        if (found == mEquipment.end())
            return std::nullopt;
        return found->second;
    }

    std::size_t ActorStateLedger::eraseCell(const std::string& cell) noexcept
    {
        std::size_t erased = 0;
        for (auto actor = mEquipment.begin(); actor != mEquipment.end();)
        {
            if (actor->first.cell == cell)
            {
                actor = mEquipment.erase(actor);
                ++erased;
            }
            else
                ++actor;
        }
        return erased;
    }

    void ActorStateLedger::clear() noexcept
    {
        mEquipment.clear();
    }

    std::size_t ActorStateLedger::size() const noexcept
    {
        return mEquipment.size();
    }

    ActorStateResult ActorStateLedger::validate(
        const std::vector<ActorEquipmentUpdate>& updates) const
    {
        if (updates.empty() || updates.size() > MaximumChanges)
            return { ActorStateDecision::InvalidBatch, mEquipment.size() };

        std::unordered_set<ActorIdentity, ActorIdentityHash> identities;
        std::size_t newActors = 0;
        for (const ActorEquipmentUpdate& update : updates)
        {
            if (!validIdentity(update.identity))
                return { ActorStateDecision::InvalidIdentity, mEquipment.size() };
            if (!identities.insert(update.identity).second)
                return { ActorStateDecision::DuplicateActor, mEquipment.size() };
            if (!validEquipment(update.equipment))
                return { ActorStateDecision::InvalidEquipment, mEquipment.size() };
            if (!mEquipment.contains(update.identity))
                ++newActors;
        }
        if (newActors > mMaximumActors - std::min(mMaximumActors, mEquipment.size()))
            return { ActorStateDecision::ActorLimitReached, mEquipment.size() };
        return { ActorStateDecision::Applied, mEquipment.size() + newActors };
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
            case ActorStateDecision::ActorLimitReached:
                return "the canonical actor limit was reached";
        }
        return "unknown actor state decision";
    }
}
