#ifndef OPENMW_MP_MECHANICS_EQUIPMENT_LEDGER_HPP
#define OPENMW_MP_MECHANICS_EQUIPMENT_LEDGER_HPP

#include "InventoryLedger.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace mwmp::mechanics
{
    struct EquipmentItem
    {
        std::string refId;
        std::int64_t count = 0;
        std::int32_t charge = -1;
        double enchantmentCharge = -1;

        bool empty() const noexcept { return refId.empty(); }
    };

    struct EquipmentChange
    {
        std::size_t slot = 0;
        EquipmentItem item;
    };

    enum class EquipmentDecision : std::uint8_t
    {
        Applied,
        InvalidOwner,
        MissingInventory,
        InvalidChangeSet,
        InvalidSlot,
        DuplicateSlot,
        InvalidItem,
        ItemNotInInventory,
    };

    struct EquipmentResult
    {
        EquipmentDecision decision = EquipmentDecision::InvalidChangeSet;

        bool applied() const noexcept { return decision == EquipmentDecision::Applied; }
    };

    class EquipmentLedger
    {
    public:
        static constexpr std::size_t SlotCount = 19;
        using Equipment = std::array<EquipmentItem, SlotCount>;

        EquipmentResult preview(std::uint64_t owner, bool fullSnapshot,
            const std::vector<EquipmentChange>& changes,
            const std::vector<InventoryItem>& inventory) const;
        EquipmentResult apply(std::uint64_t owner, bool fullSnapshot,
            const std::vector<EquipmentChange>& changes,
            const std::vector<InventoryItem>& inventory);
        EquipmentResult validateInventory(std::uint64_t owner,
            const std::vector<InventoryItem>& inventory) const;

        std::optional<Equipment> snapshot(std::uint64_t owner) const;
        bool erase(std::uint64_t owner) noexcept;
        void clear() noexcept;
        std::size_t size() const noexcept;

    private:
        EquipmentResult prepare(std::uint64_t owner, bool fullSnapshot,
            const std::vector<EquipmentChange>& changes,
            const std::vector<InventoryItem>& inventory, Equipment& candidate) const;
        static bool validItem(const EquipmentItem& item) noexcept;
        static bool inventoryContains(const std::vector<InventoryItem>& inventory,
            const Equipment& equipment) noexcept;

        std::unordered_map<std::uint64_t, Equipment> mEquipment;
    };

    const char* describe(EquipmentDecision decision) noexcept;
}

#endif
