#ifndef OPENMW_MP_MECHANICS_ITEM_USE_VALIDATOR_HPP
#define OPENMW_MP_MECHANICS_ITEM_USE_VALIDATOR_HPP

#include "InventoryLedger.hpp"

#include <cstdint>
#include <optional>
#include <vector>

namespace mwmp::mechanics
{
    struct ItemUseIntent
    {
        InventoryItem item;
        bool usingItemMagic = false;
        std::int32_t drawState = 0;
    };

    enum class ItemUseDecision : std::uint8_t
    {
        Accepted,
        MissingInventory,
        InvalidItem,
        MissingItem,
        InvalidDrawState,
    };

    class ItemUseValidator
    {
    public:
        // Mirrors MWMechanics::DrawState without making the shared mechanics
        // library depend on the rendering/gameplay executable.
        static constexpr std::int32_t MinimumDrawState = 0;
        static constexpr std::int32_t MaximumDrawState = 2;

        ItemUseDecision validate(const ItemUseIntent& intent,
            const std::optional<std::vector<InventoryItem>>& inventory) const noexcept;
    };

    const char* describe(ItemUseDecision decision) noexcept;
}

#endif
