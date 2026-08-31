#include "AuthorityLease.hpp"

#include <algorithm>
#include <limits>
#include <utility>

namespace mwmp::session
{
    AuthorityLeaseManager::AuthorityLeaseManager(std::size_t capacity)
        : mCapacity(capacity)
    {
    }

    LeaseGrantResult AuthorityLeaseManager::grant(
        std::string cell, std::uint64_t owner, Clock::time_point now)
    {
        if (cell.empty() || cell.size() > MaximumCellNameBytes)
            return { LeaseGrantDecision::InvalidCell, std::nullopt };
        if (owner == 0)
            return { LeaseGrantDecision::InvalidOwner, std::nullopt };

        const auto existing = mLeases.find(cell);
        if (existing != mLeases.end())
        {
            if (now >= existing->second.expiresAt)
                mLeases.erase(existing);
            else if (existing->second.owner == owner)
                return { LeaseGrantDecision::Existing, existing->second };
            else
                return { LeaseGrantDecision::Occupied, existing->second };
        }

        if (mLeases.size() >= mCapacity)
            return { LeaseGrantDecision::CapacityReached, std::nullopt };

        AuthorityLease lease{
            .cell = std::move(cell),
            .owner = owner,
            .leaseId = nextLeaseId(),
            .expiresAt = now + LeaseDuration,
        };
        const auto [inserted, wasInserted] = mLeases.emplace(lease.cell, lease);
        if (!wasInserted)
            return { LeaseGrantDecision::Occupied, inserted->second };
        return { LeaseGrantDecision::Granted, inserted->second };
    }

    LeaseValidation AuthorityLeaseManager::renew(std::string_view cell, std::uint64_t owner,
        std::uint64_t leaseId, Clock::time_point now)
    {
        const auto existing = mLeases.find(std::string(cell));
        if (existing == mLeases.end())
            return LeaseValidation::NotFound;
        if (now >= existing->second.expiresAt)
        {
            mLeases.erase(existing);
            return LeaseValidation::Expired;
        }
        if (existing->second.owner != owner)
            return LeaseValidation::WrongOwner;
        if (existing->second.leaseId != leaseId)
            return LeaseValidation::WrongLease;

        existing->second.expiresAt = now + LeaseDuration;
        return LeaseValidation::Valid;
    }

    LeaseValidation AuthorityLeaseManager::validate(std::string_view cell, std::uint64_t owner,
        std::uint64_t leaseId, Clock::time_point now) const
    {
        const auto existing = mLeases.find(std::string(cell));
        if (existing == mLeases.end())
            return LeaseValidation::NotFound;
        if (now >= existing->second.expiresAt)
            return LeaseValidation::Expired;
        if (existing->second.owner != owner)
            return LeaseValidation::WrongOwner;
        if (existing->second.leaseId != leaseId)
            return LeaseValidation::WrongLease;
        return LeaseValidation::Valid;
    }

    bool AuthorityLeaseManager::release(
        std::string_view cell, std::uint64_t owner, std::uint64_t leaseId)
    {
        const auto existing = mLeases.find(std::string(cell));
        if (existing == mLeases.end() || existing->second.owner != owner
            || existing->second.leaseId != leaseId)
            return false;
        mLeases.erase(existing);
        return true;
    }

    std::size_t AuthorityLeaseManager::releaseOwner(std::uint64_t owner)
    {
        return std::erase_if(mLeases,
            [owner](const auto& entry) { return entry.second.owner == owner; });
    }

    std::vector<AuthorityLease> AuthorityLeaseManager::expire(Clock::time_point now)
    {
        std::vector<AuthorityLease> expired;
        for (auto lease = mLeases.begin(); lease != mLeases.end();)
        {
            if (now < lease->second.expiresAt)
            {
                ++lease;
                continue;
            }
            expired.push_back(std::move(lease->second));
            lease = mLeases.erase(lease);
        }
        return expired;
    }

    std::optional<AuthorityLease> AuthorityLeaseManager::find(std::string_view cell) const
    {
        const auto existing = mLeases.find(std::string(cell));
        if (existing == mLeases.end())
            return std::nullopt;
        return existing->second;
    }

    std::uint64_t AuthorityLeaseManager::nextLeaseId()
    {
        for (;;)
        {
            const std::uint64_t candidate = mNextLeaseId;
            if (mNextLeaseId == std::numeric_limits<std::uint64_t>::max())
                mNextLeaseId = 1;
            else
                ++mNextLeaseId;

            const bool alreadyActive = std::any_of(mLeases.begin(), mLeases.end(),
                [candidate](const auto& entry) { return entry.second.leaseId == candidate; });
            if (!alreadyActive)
                return candidate;
        }
    }
}
