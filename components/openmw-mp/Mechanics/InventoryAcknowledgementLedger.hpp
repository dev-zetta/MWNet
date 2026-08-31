#ifndef OPENMW_MP_MECHANICS_INVENTORY_ACKNOWLEDGEMENT_LEDGER_HPP
#define OPENMW_MP_MECHANICS_INVENTORY_ACKNOWLEDGEMENT_LEDGER_HPP

#include "InventoryLedger.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <unordered_map>
#include <vector>

namespace mwmp::mechanics
{
    class InventoryAcknowledgementLedger
    {
    public:
        using Clock = std::chrono::steady_clock;

        static constexpr std::size_t MaximumPendingPerPlayer = 32;
        static constexpr auto AcknowledgementLifetime = std::chrono::seconds(10);

        bool canExpect(std::uint64_t player, Clock::time_point now = Clock::now());
        bool expect(std::uint64_t player, InventoryAction action,
            const std::vector<InventoryItem>& items,
            Clock::time_point now = Clock::now());
        bool matches(std::uint64_t player, InventoryAction action,
            const std::vector<InventoryItem>& items,
            Clock::time_point now = Clock::now());
        bool consume(std::uint64_t player, InventoryAction action,
            const std::vector<InventoryItem>& items,
            Clock::time_point now = Clock::now());
        void erase(std::uint64_t player) noexcept;
        std::size_t pending(std::uint64_t player, Clock::time_point now = Clock::now());

    private:
        struct Acknowledgement
        {
            InventoryAction action = InventoryAction::Set;
            std::vector<InventoryItem> items;
            Clock::time_point expiresAt;
        };

        using Queue = std::deque<Acknowledgement>;

        bool contains(std::uint64_t player, InventoryAction action,
            const std::vector<InventoryItem>& items) const;
        Queue* prune(std::uint64_t player, Clock::time_point now);

        std::unordered_map<std::uint64_t, Queue> mPending;
    };
}

#endif
