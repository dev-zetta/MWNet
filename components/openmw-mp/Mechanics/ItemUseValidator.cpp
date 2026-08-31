#include "ItemUseValidator.hpp"

#include <algorithm>
#include <cmath>

namespace mwmp::mechanics
{
    ItemUseDecision ItemUseValidator::validate(const ItemUseIntent& intent,
        const std::optional<std::vector<InventoryItem>>& inventory) const noexcept
    {
        if (intent.drawState < MinimumDrawState || intent.drawState > MaximumDrawState)
            return ItemUseDecision::InvalidDrawState;

        const InventoryItem& item = intent.item;
        if (item.refId.empty() || item.refId.size() > InventoryLedger::MaximumStringBytes
            || item.soul.size() > InventoryLedger::MaximumStringBytes || item.charge < -1
            || !std::isfinite(item.enchantmentCharge) || item.enchantmentCharge < -1
            || item.enchantmentCharge > InventoryLedger::MaximumEnchantmentCharge
            || item.count <= 0 || item.count > InventoryLedger::MaximumStackCount)
        {
            return ItemUseDecision::InvalidItem;
        }
        if (!inventory)
            return ItemUseDecision::MissingInventory;

        const auto found = std::ranges::find_if(*inventory,
            [&item](const InventoryItem& candidate) {
                return candidate.sameStack(item) && candidate.count == item.count;
            });
        return found == inventory->end() ? ItemUseDecision::MissingItem
                                         : ItemUseDecision::Accepted;
    }

    const char* describe(ItemUseDecision decision) noexcept
    {
        switch (decision)
        {
            case ItemUseDecision::Accepted: return "the item-use intent is valid";
            case ItemUseDecision::MissingInventory:
                return "the player has no canonical inventory";
            case ItemUseDecision::InvalidItem: return "the used item is invalid";
            case ItemUseDecision::MissingItem:
                return "the exact item stack is not in the canonical inventory";
            case ItemUseDecision::InvalidDrawState:
                return "the requested draw state is invalid";
        }
        return "unknown item-use decision";
    }
}
