#include "InventoryLedger.hpp"

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>

namespace mwmp::mechanics
{
    std::size_t InventoryOwnerHash::operator()(const InventoryOwner& owner) const noexcept
    {
        std::size_t seed = std::hash<std::uint64_t>{}(owner.value)
            ^ (static_cast<std::size_t>(owner.kind) << 1);
        seed ^= std::hash<std::string>{}(owner.scope) + 0x9e3779b9 + (seed << 6)
            + (seed >> 2);
        return seed;
    }

    bool InventoryItem::sameStack(const InventoryItem& other) const noexcept
    {
        return refId == other.refId && soul == other.soul && charge == other.charge
            && enchantmentCharge == other.enchantmentCharge;
    }

    InventoryLedger::InventoryLedger(std::size_t maximumOwners)
        : mMaximumOwners(maximumOwners)
    {
    }

    InventoryResult InventoryLedger::preview(InventoryOwner owner, InventoryAction action,
        const std::vector<InventoryItem>& items) const
    {
        std::vector<InventoryItem> candidate;
        return previewSnapshot(std::move(owner), action, items, candidate);
    }

    InventoryResult InventoryLedger::previewSnapshot(InventoryOwner owner,
        InventoryAction action, const std::vector<InventoryItem>& items,
        std::vector<InventoryItem>& candidate) const
    {
        candidate.clear();
        if (!validOwner(owner))
            return { InventoryDecision::InvalidOwner };
        const auto existing = mInventories.find(owner);
        if (existing == mInventories.end() && mInventories.size() >= mMaximumOwners)
            return { InventoryDecision::OwnerLimitReached };

        if (existing != mInventories.end())
            candidate = existing->second;
        return applyTo(candidate, action, items);
    }

    InventoryResult InventoryLedger::apply(InventoryOwner owner, InventoryAction action,
        const std::vector<InventoryItem>& items)
    {
        if (!validOwner(owner))
            return { InventoryDecision::InvalidOwner };
        const auto existing = mInventories.find(owner);
        if (existing == mInventories.end() && mInventories.size() >= mMaximumOwners)
            return { InventoryDecision::OwnerLimitReached };

        std::vector<InventoryItem> candidate;
        if (existing != mInventories.end())
            candidate = existing->second;
        const InventoryResult result = applyTo(candidate, action, items);
        if (!result.applied())
            return result;
        mInventories.insert_or_assign(owner, std::move(candidate));
        return result;
    }

    InventoryResult InventoryLedger::transfer(InventoryOwner from, InventoryOwner to,
        const std::vector<InventoryItem>& items)
    {
        if (!validOwner(from) || !validOwner(to) || from == to)
            return { InventoryDecision::InvalidOwner };
        const auto source = mInventories.find(from);
        if (source == mInventories.end())
            return { InventoryDecision::MissingItem };
        const auto destination = mInventories.find(to);
        if (destination == mInventories.end() && mInventories.size() >= mMaximumOwners)
            return { InventoryDecision::OwnerLimitReached };

        std::vector<InventoryItem> sourceCandidate = source->second;
        std::vector<InventoryItem> destinationCandidate;
        if (destination != mInventories.end())
            destinationCandidate = destination->second;

        const InventoryResult removed = removeFrom(sourceCandidate, items);
        if (!removed.applied())
            return removed;
        const InventoryResult added = addTo(destinationCandidate, items);
        if (!added.applied())
            return added;

        source->second = std::move(sourceCandidate);
        mInventories.insert_or_assign(to, std::move(destinationCandidate));
        return { InventoryDecision::Applied, added.stackCount };
    }

    InventoryResult InventoryLedger::previewBatch(
        const std::vector<InventoryOperation>& operations) const
    {
        std::unordered_map<InventoryOwner, std::vector<InventoryItem>, InventoryOwnerHash>
            candidates;
        return prepareBatch(operations, candidates);
    }

    InventoryResult InventoryLedger::applyBatch(
        const std::vector<InventoryOperation>& operations)
    {
        std::unordered_map<InventoryOwner, std::vector<InventoryItem>, InventoryOwnerHash>
            candidates;
        const InventoryResult result = prepareBatch(operations, candidates);
        if (!result.applied())
            return result;
        for (auto& [owner, inventory] : candidates)
            mInventories.insert_or_assign(std::move(owner), std::move(inventory));
        return result;
    }

    std::optional<std::vector<InventoryItem>> InventoryLedger::snapshot(
        InventoryOwner owner) const
    {
        const auto found = mInventories.find(owner);
        if (found == mInventories.end())
            return std::nullopt;
        return found->second;
    }

    bool InventoryLedger::erase(InventoryOwner owner) noexcept
    {
        return mInventories.erase(owner) != 0;
    }

    void InventoryLedger::swap(InventoryLedger& other) noexcept
    {
        std::swap(mMaximumOwners, other.mMaximumOwners);
        mInventories.swap(other.mInventories);
    }

    void InventoryLedger::clear() noexcept
    {
        mInventories.clear();
    }

    std::size_t InventoryLedger::size() const noexcept
    {
        return mInventories.size();
    }

    bool InventoryLedger::validOwner(InventoryOwner owner) noexcept
    {
        if (owner.value == 0)
            return false;
        if (owner.kind == InventoryOwnerKind::Player)
            return owner.scope.empty();
        return !owner.scope.empty();
    }

    bool InventoryLedger::validItem(const InventoryItem& item) noexcept
    {
        return !item.refId.empty() && item.refId.size() <= MaximumStringBytes
            && item.soul.size() <= MaximumStringBytes && item.charge >= -1
            && std::isfinite(item.enchantmentCharge) && item.enchantmentCharge >= -1
            && item.enchantmentCharge <= MaximumEnchantmentCharge
            && item.count > 0 && item.count <= MaximumStackCount;
    }

    InventoryResult InventoryLedger::applyTo(std::vector<InventoryItem>& inventory,
        InventoryAction action, const std::vector<InventoryItem>& items)
    {
        if (items.size() > MaximumStacks)
            return { InventoryDecision::ItemLimitReached, inventory.size() };
        switch (action)
        {
            case InventoryAction::Set:
            {
                std::vector<InventoryItem> replacement;
                const InventoryResult result = addTo(replacement, items);
                if (result.applied())
                    inventory = std::move(replacement);
                return result;
            }
            case InventoryAction::Add:
                return addTo(inventory, items);
            case InventoryAction::Remove:
                return removeFrom(inventory, items);
        }
        return { InventoryDecision::InvalidAction, inventory.size() };
    }

    InventoryResult InventoryLedger::addTo(std::vector<InventoryItem>& inventory,
        const std::vector<InventoryItem>& items)
    {
        for (const InventoryItem& item : items)
        {
            if (!validItem(item))
                return { InventoryDecision::InvalidItem, inventory.size() };
            const auto existing = std::find_if(inventory.begin(), inventory.end(),
                [&item](const InventoryItem& candidate) { return candidate.sameStack(item); });
            if (existing == inventory.end())
            {
                if (inventory.size() >= MaximumStacks)
                    return { InventoryDecision::ItemLimitReached, inventory.size() };
                inventory.push_back(item);
                continue;
            }
            if (item.count > MaximumStackCount - existing->count)
                return { InventoryDecision::CountOverflow, inventory.size() };
            existing->count += item.count;
        }
        return { InventoryDecision::Applied, inventory.size() };
    }

    InventoryResult InventoryLedger::removeFrom(std::vector<InventoryItem>& inventory,
        const std::vector<InventoryItem>& items)
    {
        for (const InventoryItem& item : items)
        {
            if (!validItem(item))
                return { InventoryDecision::InvalidItem, inventory.size() };
            const auto existing = std::find_if(inventory.begin(), inventory.end(),
                [&item](const InventoryItem& candidate) { return candidate.sameStack(item); });
            if (existing == inventory.end())
                return { InventoryDecision::MissingItem, inventory.size() };
            if (existing->count < item.count)
                return { InventoryDecision::InsufficientItems, inventory.size() };
            existing->count -= item.count;
            if (existing->count == 0)
                inventory.erase(existing);
        }
        return { InventoryDecision::Applied, inventory.size() };
    }

    InventoryResult InventoryLedger::prepareBatch(
        const std::vector<InventoryOperation>& operations,
        std::unordered_map<InventoryOwner, std::vector<InventoryItem>, InventoryOwnerHash>&
            candidates) const
    {
        InventoryResult result{ InventoryDecision::Applied };
        std::size_t newOwnerCount = 0;
        for (const InventoryOperation& operation : operations)
        {
            if (!validOwner(operation.owner))
                return { InventoryDecision::InvalidOwner };

            auto candidate = candidates.find(operation.owner);
            if (candidate == candidates.end())
            {
                const auto existing = mInventories.find(operation.owner);
                if (existing == mInventories.end()
                    && mInventories.size() + newOwnerCount >= mMaximumOwners)
                {
                    return { InventoryDecision::OwnerLimitReached };
                }
                if (existing == mInventories.end())
                    ++newOwnerCount;
                candidate = candidates.emplace(operation.owner,
                    existing == mInventories.end() ? std::vector<InventoryItem>{}
                                                   : existing->second).first;
            }

            result = applyTo(candidate->second, operation.action, operation.items);
            if (!result.applied())
                return result;
        }
        return result;
    }

    const char* describe(InventoryDecision decision) noexcept
    {
        switch (decision)
        {
            case InventoryDecision::Applied:
                return "the inventory action was applied";
            case InventoryDecision::InvalidOwner:
                return "the inventory owner is invalid";
            case InventoryDecision::InvalidAction:
                return "the inventory action is invalid";
            case InventoryDecision::InvalidItem:
                return "an inventory item is invalid";
            case InventoryDecision::CountOverflow:
                return "an inventory stack exceeds its count limit";
            case InventoryDecision::MissingItem:
                return "an inventory item does not exist";
            case InventoryDecision::InsufficientItems:
                return "the inventory does not contain the requested count";
            case InventoryDecision::ItemLimitReached:
                return "the inventory stack limit was reached";
            case InventoryDecision::OwnerLimitReached:
                return "the inventory owner limit was reached";
        }
        return "unknown inventory decision";
    }
}
