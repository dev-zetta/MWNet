#ifndef OPENMW_MP_MECHANICS_INVENTORY_LEDGER_HPP
#define OPENMW_MP_MECHANICS_INVENTORY_LEDGER_HPP

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace mwmp::mechanics
{
    enum class InventoryOwnerKind : std::uint8_t
    {
        Player,
        Container,
    };

    struct InventoryOwner
    {
        InventoryOwnerKind kind = InventoryOwnerKind::Player;
        std::uint64_t value = 0;

        bool operator==(const InventoryOwner&) const = default;
    };

    struct InventoryOwnerHash
    {
        std::size_t operator()(const InventoryOwner& owner) const noexcept;
    };

    struct InventoryItem
    {
        std::string refId;
        std::string soul;
        std::int32_t charge = -1;
        double enchantmentCharge = -1;
        std::int64_t count = 0;

        bool sameStack(const InventoryItem& other) const noexcept;
    };

    enum class InventoryAction : std::uint8_t
    {
        Set,
        Add,
        Remove,
    };

    enum class InventoryDecision : std::uint8_t
    {
        Applied,
        InvalidOwner,
        InvalidAction,
        InvalidItem,
        CountOverflow,
        MissingItem,
        InsufficientItems,
        ItemLimitReached,
        OwnerLimitReached,
    };

    struct InventoryResult
    {
        InventoryDecision decision = InventoryDecision::InvalidAction;
        std::size_t stackCount = 0;

        bool applied() const noexcept { return decision == InventoryDecision::Applied; }
    };

    class InventoryLedger
    {
    public:
        static constexpr std::size_t MaximumStacks = 4096;
        static constexpr std::size_t MaximumStringBytes = 4096;
        static constexpr std::int64_t MaximumStackCount = 1'000'000'000;
        static constexpr double MaximumEnchantmentCharge = 1'000'000'000.0;

        explicit InventoryLedger(std::size_t maximumOwners = 8192);

        InventoryResult preview(InventoryOwner owner, InventoryAction action,
            const std::vector<InventoryItem>& items) const;
        InventoryResult apply(InventoryOwner owner, InventoryAction action,
            const std::vector<InventoryItem>& items);
        InventoryResult transfer(InventoryOwner from, InventoryOwner to,
            const std::vector<InventoryItem>& items);

        std::optional<std::vector<InventoryItem>> snapshot(InventoryOwner owner) const;
        bool erase(InventoryOwner owner) noexcept;
        void clear() noexcept;
        std::size_t size() const noexcept;

    private:
        static bool validOwner(InventoryOwner owner) noexcept;
        static bool validItem(const InventoryItem& item) noexcept;
        static InventoryResult applyTo(std::vector<InventoryItem>& inventory,
            InventoryAction action, const std::vector<InventoryItem>& items);
        static InventoryResult addTo(std::vector<InventoryItem>& inventory,
            const std::vector<InventoryItem>& items);
        static InventoryResult removeFrom(std::vector<InventoryItem>& inventory,
            const std::vector<InventoryItem>& items);

        std::size_t mMaximumOwners;
        std::unordered_map<InventoryOwner, std::vector<InventoryItem>, InventoryOwnerHash>
            mInventories;
    };

    const char* describe(InventoryDecision decision) noexcept;
}

#endif
