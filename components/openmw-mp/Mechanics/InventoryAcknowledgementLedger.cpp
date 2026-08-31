#include "InventoryAcknowledgementLedger.hpp"

#include <algorithm>

namespace mwmp::mechanics
{
    bool InventoryAcknowledgementLedger::canExpect(
        std::uint64_t player, Clock::time_point now)
    {
        Queue* queue = prune(player, now);
        return player != 0 && (queue == nullptr || queue->size() < MaximumPendingPerPlayer);
    }

    bool InventoryAcknowledgementLedger::expect(std::uint64_t player,
        InventoryAction action, const std::vector<InventoryItem>& items,
        Clock::time_point now)
    {
        if (items.empty() || !canExpect(player, now))
            return false;
        mPending[player].push_back({ action, items, now + AcknowledgementLifetime });
        return true;
    }

    bool InventoryAcknowledgementLedger::matches(std::uint64_t player,
        InventoryAction action, const std::vector<InventoryItem>& items,
        Clock::time_point now)
    {
        return prune(player, now) != nullptr && contains(player, action, items);
    }

    bool InventoryAcknowledgementLedger::consume(std::uint64_t player,
        InventoryAction action, const std::vector<InventoryItem>& items,
        Clock::time_point now)
    {
        if (prune(player, now) == nullptr || !contains(player, action, items))
            return false;

        Queue& queue = mPending.at(player);
        for (const InventoryItem& requested : items)
        {
            std::int64_t remaining = requested.count;
            for (Acknowledgement& acknowledgement : queue)
            {
                if (acknowledgement.action != action)
                    continue;
                for (InventoryItem& available : acknowledgement.items)
                {
                    if (!available.sameStack(requested))
                        continue;
                    const std::int64_t consumed = std::min(available.count, remaining);
                    available.count -= consumed;
                    remaining -= consumed;
                    if (remaining == 0)
                        break;
                }
                if (remaining == 0)
                    break;
            }
        }
        for (Acknowledgement& acknowledgement : queue)
        {
            std::erase_if(acknowledgement.items,
                [](const InventoryItem& item) { return item.count == 0; });
        }
        std::erase_if(queue,
            [](const Acknowledgement& acknowledgement)
            {
                return acknowledgement.items.empty();
            });
        if (queue.empty())
            mPending.erase(player);
        return true;
    }

    void InventoryAcknowledgementLedger::erase(std::uint64_t player) noexcept
    {
        mPending.erase(player);
    }

    std::size_t InventoryAcknowledgementLedger::pending(
        std::uint64_t player, Clock::time_point now)
    {
        Queue* queue = prune(player, now);
        return queue == nullptr ? 0 : queue->size();
    }

    bool InventoryAcknowledgementLedger::contains(std::uint64_t player,
        InventoryAction action, const std::vector<InventoryItem>& items) const
    {
        const auto found = mPending.find(player);
        if (found == mPending.end() || items.empty())
            return false;
        for (std::size_t requestedIndex = 0; requestedIndex < items.size();
             ++requestedIndex)
        {
            const InventoryItem& requested = items[requestedIndex];
            if (requested.count <= 0)
                return false;
            if (std::any_of(items.begin(), items.begin() + requestedIndex,
                    [&](const InventoryItem& earlier)
                    {
                        return earlier.sameStack(requested);
                    }))
            {
                continue;
            }
            std::int64_t requestedCount = 0;
            for (const InventoryItem& candidate : items)
            {
                if (candidate.sameStack(requested))
                {
                    if (candidate.count <= 0
                        || candidate.count > InventoryLedger::MaximumStackCount
                            - requestedCount)
                    {
                        return false;
                    }
                    requestedCount += candidate.count;
                }
            }
            std::int64_t availableCount = 0;
            for (const Acknowledgement& acknowledgement : found->second)
            {
                if (acknowledgement.action != action)
                    continue;
                for (const InventoryItem& available : acknowledgement.items)
                {
                    if (available.sameStack(requested))
                        availableCount += available.count;
                }
            }
            if (availableCount < requestedCount)
                return false;
        }
        return true;
    }

    InventoryAcknowledgementLedger::Queue* InventoryAcknowledgementLedger::prune(
        std::uint64_t player, Clock::time_point now)
    {
        const auto found = mPending.find(player);
        if (found == mPending.end())
            return nullptr;
        std::erase_if(found->second,
            [now](const Acknowledgement& acknowledgement)
            {
                return acknowledgement.expiresAt <= now;
            });
        if (found->second.empty())
        {
            mPending.erase(found);
            return nullptr;
        }
        return &found->second;
    }
}
