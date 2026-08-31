#include "EquipmentLedger.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace mwmp::mechanics
{
    EquipmentResult EquipmentLedger::preview(std::uint64_t owner, bool fullSnapshot,
        const std::vector<EquipmentChange>& changes,
        const std::vector<InventoryItem>& inventory) const
    {
        Equipment candidate;
        return prepare(owner, fullSnapshot, changes, inventory, candidate);
    }

    EquipmentResult EquipmentLedger::apply(std::uint64_t owner, bool fullSnapshot,
        const std::vector<EquipmentChange>& changes,
        const std::vector<InventoryItem>& inventory)
    {
        Equipment candidate;
        const EquipmentResult result
            = prepare(owner, fullSnapshot, changes, inventory, candidate);
        if (result.applied())
            mEquipment.insert_or_assign(owner, std::move(candidate));
        return result;
    }

    EquipmentResult EquipmentLedger::validateInventory(std::uint64_t owner,
        const std::vector<InventoryItem>& inventory) const
    {
        if (owner == 0)
            return { EquipmentDecision::InvalidOwner };
        const auto existing = mEquipment.find(owner);
        if (existing == mEquipment.end())
            return { EquipmentDecision::Applied };
        if (!inventoryContains(inventory, existing->second))
            return { EquipmentDecision::ItemNotInInventory };
        return { EquipmentDecision::Applied };
    }

    std::optional<EquipmentLedger::Equipment> EquipmentLedger::snapshot(
        std::uint64_t owner) const
    {
        const auto found = mEquipment.find(owner);
        if (found == mEquipment.end())
            return std::nullopt;
        return found->second;
    }

    bool EquipmentLedger::erase(std::uint64_t owner) noexcept
    {
        return mEquipment.erase(owner) != 0;
    }

    void EquipmentLedger::swap(EquipmentLedger& other) noexcept
    {
        mEquipment.swap(other.mEquipment);
    }

    void EquipmentLedger::clear() noexcept
    {
        mEquipment.clear();
    }

    std::size_t EquipmentLedger::size() const noexcept
    {
        return mEquipment.size();
    }

    EquipmentResult EquipmentLedger::prepare(std::uint64_t owner, bool fullSnapshot,
        const std::vector<EquipmentChange>& changes,
        const std::vector<InventoryItem>& inventory, Equipment& candidate) const
    {
        if (owner == 0)
            return { EquipmentDecision::InvalidOwner };
        if (changes.empty() || changes.size() > SlotCount
            || (fullSnapshot && changes.size() != SlotCount))
        {
            return { EquipmentDecision::InvalidChangeSet };
        }

        const auto existing = mEquipment.find(owner);
        if (!fullSnapshot && existing != mEquipment.end())
            candidate = existing->second;

        std::array<bool, SlotCount> changed{};
        for (const EquipmentChange& change : changes)
        {
            if (change.slot >= SlotCount)
                return { EquipmentDecision::InvalidSlot };
            if (changed[change.slot])
                return { EquipmentDecision::DuplicateSlot };
            if (!validItem(change.item))
                return { EquipmentDecision::InvalidItem };
            changed[change.slot] = true;
            candidate[change.slot] = change.item;
        }
        if (fullSnapshot
            && std::find(changed.begin(), changed.end(), false) != changed.end())
        {
            return { EquipmentDecision::InvalidChangeSet };
        }
        if (!inventoryContains(inventory, candidate))
            return { EquipmentDecision::ItemNotInInventory };
        return { EquipmentDecision::Applied };
    }

    bool EquipmentLedger::validItem(const EquipmentItem& item) noexcept
    {
        if (item.empty())
            return item.count == 0;
        return item.refId.size() <= InventoryLedger::MaximumStringBytes
            && item.count > 0 && item.count <= InventoryLedger::MaximumStackCount
            && item.charge >= -1 && std::isfinite(item.enchantmentCharge)
            && item.enchantmentCharge >= -1
            && item.enchantmentCharge <= InventoryLedger::MaximumEnchantmentCharge;
    }

    bool EquipmentLedger::inventoryContains(const std::vector<InventoryItem>& inventory,
        const Equipment& equipment) noexcept
    {
        for (std::size_t slot = 0; slot < equipment.size(); ++slot)
        {
            const EquipmentItem& equipped = equipment[slot];
            if (equipped.empty())
                continue;

            std::int64_t required = 0;
            for (std::size_t other = 0; other < equipment.size(); ++other)
            {
                const EquipmentItem& candidate = equipment[other];
                if (candidate.refId != equipped.refId
                    || candidate.charge != equipped.charge
                    || candidate.enchantmentCharge != equipped.enchantmentCharge)
                {
                    continue;
                }
                if (candidate.count > std::numeric_limits<std::int64_t>::max() - required)
                    return false;
                required += candidate.count;
            }

            std::int64_t available = 0;
            for (const InventoryItem& candidate : inventory)
            {
                if (candidate.refId != equipped.refId
                    || candidate.charge != equipped.charge
                    || candidate.enchantmentCharge != equipped.enchantmentCharge)
                {
                    continue;
                }
                if (candidate.count > std::numeric_limits<std::int64_t>::max() - available)
                    return false;
                available += candidate.count;
            }
            if (available < required)
                return false;

            // An identical equipped stack was already checked from its first slot.
            while (++slot < equipment.size())
            {
                const EquipmentItem& next = equipment[slot];
                if (next.refId != equipped.refId || next.charge != equipped.charge
                    || next.enchantmentCharge != equipped.enchantmentCharge)
                {
                    --slot;
                    break;
                }
            }
        }
        return true;
    }

    const char* describe(EquipmentDecision decision) noexcept
    {
        switch (decision)
        {
            case EquipmentDecision::Applied:
                return "the equipment action was applied";
            case EquipmentDecision::InvalidOwner:
                return "the equipment owner is invalid";
            case EquipmentDecision::MissingInventory:
                return "the canonical inventory is missing";
            case EquipmentDecision::InvalidChangeSet:
                return "the equipment change set is invalid";
            case EquipmentDecision::InvalidSlot:
                return "an equipment slot is invalid";
            case EquipmentDecision::DuplicateSlot:
                return "an equipment slot occurs more than once";
            case EquipmentDecision::InvalidItem:
                return "an equipment item is invalid";
            case EquipmentDecision::ItemNotInInventory:
                return "an equipped item is not present in the canonical inventory";
        }
        return "unknown equipment decision";
    }
}
