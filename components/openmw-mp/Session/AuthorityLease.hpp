#ifndef OPENMW_MP_SESSION_AUTHORITY_LEASE_HPP
#define OPENMW_MP_SESSION_AUTHORITY_LEASE_HPP

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace mwmp::session
{
    struct AuthorityLease
    {
        using Clock = std::chrono::steady_clock;

        std::string cell;
        std::uint64_t owner = 0;
        std::uint64_t leaseId = 0;
        Clock::time_point expiresAt{};
    };

    enum class LeaseGrantDecision : std::uint8_t
    {
        Granted,
        Existing,
        Occupied,
        InvalidCell,
        InvalidOwner,
        CapacityReached,
    };

    enum class LeaseValidation : std::uint8_t
    {
        Valid,
        NotFound,
        Expired,
        WrongOwner,
        WrongLease,
    };

    struct LeaseGrantResult
    {
        LeaseGrantDecision decision = LeaseGrantDecision::CapacityReached;
        std::optional<AuthorityLease> lease;
    };

    class AuthorityLeaseManager
    {
    public:
        using Clock = AuthorityLease::Clock;

        static constexpr std::chrono::seconds RenewalInterval{ 2 };
        static constexpr std::chrono::seconds LeaseDuration{ 5 };
        static constexpr std::size_t MaximumCellNameBytes = 512;

        explicit AuthorityLeaseManager(std::size_t capacity = 4096);

        LeaseGrantResult grant(std::string cell, std::uint64_t owner, Clock::time_point now);
        LeaseValidation renew(std::string_view cell, std::uint64_t owner,
            std::uint64_t leaseId, Clock::time_point now);
        LeaseValidation validate(std::string_view cell, std::uint64_t owner,
            std::uint64_t leaseId, Clock::time_point now) const;

        bool release(std::string_view cell, std::uint64_t owner, std::uint64_t leaseId);
        std::size_t releaseOwner(std::uint64_t owner);
        std::vector<AuthorityLease> expire(Clock::time_point now);

        std::optional<AuthorityLease> find(std::string_view cell) const;
        std::size_t size() const noexcept { return mLeases.size(); }
        std::size_t capacity() const noexcept { return mCapacity; }

    private:
        std::uint64_t nextLeaseId();

        std::size_t mCapacity;
        std::uint64_t mNextLeaseId = 1;
        std::unordered_map<std::string, AuthorityLease> mLeases;
    };
}

#endif
