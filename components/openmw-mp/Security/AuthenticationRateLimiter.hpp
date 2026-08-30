#ifndef OPENMW_MP_SECURITY_AUTHENTICATION_RATE_LIMITER_HPP
#define OPENMW_MP_SECURITY_AUTHENTICATION_RATE_LIMITER_HPP

#include <chrono>
#include <cstddef>
#include <deque>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>

namespace mwmp::security
{
    struct AuthenticationLimits
    {
        std::size_t failuresBeforeLockout = 5;
        std::chrono::minutes failureWindow{ 15 };
        std::chrono::minutes lockoutDuration{ 15 };
        double preKdfAttemptsPerMinute = 20.0;
        double preKdfBurst = 10.0;
        std::size_t maximumTrackedEntries = 4096;
    };

    enum class AuthenticationAttemptDecision
    {
        Allowed,
        InvalidIdentity,
        AccountAndAddressLocked,
        PreKdfRateLimited,
        CapacityReached,
    };

    class AuthenticationRateLimiter
    {
    public:
        using Clock = std::chrono::steady_clock;

        explicit AuthenticationRateLimiter(AuthenticationLimits limits = {});

        AuthenticationAttemptDecision begin(std::string_view account, std::string_view address,
            Clock::time_point now = Clock::now());
        void recordFailure(std::string_view account, std::string_view address,
            Clock::time_point now = Clock::now());
        void recordSuccess(std::string_view account, std::string_view address);
        std::size_t trackedAccountAddresses() const;
        std::size_t trackedAddresses() const;

    private:
        struct AccountAddress
        {
            std::string account;
            std::string address;

            bool operator==(const AccountAddress&) const = default;
        };

        struct AccountAddressHash
        {
            std::size_t operator()(const AccountAddress& value) const noexcept;
        };

        struct FailureState
        {
            std::deque<Clock::time_point> failures;
            Clock::time_point lockedUntil{};
            Clock::time_point lastSeen{};
        };

        struct AddressState
        {
            double tokens = 0.0;
            Clock::time_point lastRefill{};
            Clock::time_point lastSeen{};
        };

        static bool validIdentity(std::string_view account, std::string_view address) noexcept;
        static AccountAddress key(std::string_view account, std::string_view address);
        void prune(Clock::time_point now);

        AuthenticationLimits mLimits;
        mutable std::mutex mMutex;
        std::unordered_map<AccountAddress, FailureState, AccountAddressHash> mFailures;
        std::unordered_map<std::string, AddressState> mAddresses;
    };
}

#endif
