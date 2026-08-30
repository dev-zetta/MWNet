#include "AuthenticationRateLimiter.hpp"

#include <components/openmw-mp/Protocol/ProtocolLimits.hpp>

#include <algorithm>
#include <cctype>
#include <functional>

namespace mwmp::security
{
    AuthenticationRateLimiter::AuthenticationRateLimiter(AuthenticationLimits limits)
        : mLimits(limits)
    {
    }

    std::size_t AuthenticationRateLimiter::AccountAddressHash::operator()(
        const AccountAddress& value) const noexcept
    {
        const std::size_t account = std::hash<std::string>{}(value.account);
        const std::size_t address = std::hash<std::string>{}(value.address);
        return account ^ (address + 0x9e3779b9U + (account << 6U) + (account >> 2U));
    }

    bool AuthenticationRateLimiter::validIdentity(
        std::string_view account, std::string_view address) noexcept
    {
        return !account.empty() && account.size() <= protocol::limits::accountNameBytes
            && !address.empty() && address.size() <= 128;
    }

    AuthenticationRateLimiter::AccountAddress AuthenticationRateLimiter::key(
        std::string_view account, std::string_view address)
    {
        AccountAddress result;
        result.account.reserve(account.size());
        for (const unsigned char character : account)
            result.account.push_back(static_cast<char>(std::tolower(character)));
        result.address = address;
        return result;
    }

    void AuthenticationRateLimiter::prune(Clock::time_point now)
    {
        const auto retention = mLimits.failureWindow + mLimits.lockoutDuration;
        std::erase_if(mFailures, [&](const auto& entry) {
            return entry.second.lockedUntil <= now
                && now - entry.second.lastSeen > retention;
        });
        std::erase_if(mAddresses, [&](const auto& entry) {
            return now - entry.second.lastSeen > retention;
        });
    }

    AuthenticationAttemptDecision AuthenticationRateLimiter::begin(
        std::string_view account, std::string_view address, Clock::time_point now)
    {
        if (!validIdentity(account, address) || mLimits.failuresBeforeLockout == 0
            || mLimits.preKdfAttemptsPerMinute <= 0.0 || mLimits.preKdfBurst < 1.0
            || mLimits.maximumTrackedEntries == 0)
            return AuthenticationAttemptDecision::InvalidIdentity;

        std::scoped_lock lock(mMutex);
        prune(now);
        const AccountAddress accountAddress = key(account, address);
        auto failure = mFailures.find(accountAddress);
        if (failure == mFailures.end())
        {
            if (mFailures.size() >= mLimits.maximumTrackedEntries)
                return AuthenticationAttemptDecision::CapacityReached;
            failure = mFailures.emplace(accountAddress, FailureState{}).first;
        }
        failure->second.lastSeen = now;
        if (failure->second.lockedUntil > now)
            return AuthenticationAttemptDecision::AccountAndAddressLocked;

        auto addressState = mAddresses.find(std::string(address));
        if (addressState == mAddresses.end())
        {
            if (mAddresses.size() >= mLimits.maximumTrackedEntries)
                return AuthenticationAttemptDecision::CapacityReached;
            AddressState initial;
            initial.tokens = mLimits.preKdfBurst;
            initial.lastRefill = now;
            initial.lastSeen = now;
            addressState = mAddresses.emplace(address, initial).first;
        }

        AddressState& state = addressState->second;
        const double elapsedMinutes = std::chrono::duration<double, std::ratio<60>>(
            now - state.lastRefill).count();
        state.tokens = std::min(mLimits.preKdfBurst,
            state.tokens + elapsedMinutes * mLimits.preKdfAttemptsPerMinute);
        state.lastRefill = now;
        state.lastSeen = now;
        if (state.tokens < 1.0)
            return AuthenticationAttemptDecision::PreKdfRateLimited;
        state.tokens -= 1.0;
        return AuthenticationAttemptDecision::Allowed;
    }

    void AuthenticationRateLimiter::recordFailure(
        std::string_view account, std::string_view address, Clock::time_point now)
    {
        if (!validIdentity(account, address))
            return;
        std::scoped_lock lock(mMutex);
        const auto found = mFailures.find(key(account, address));
        if (found == mFailures.end())
            return;

        FailureState& state = found->second;
        state.lastSeen = now;
        while (!state.failures.empty() && now - state.failures.front() > mLimits.failureWindow)
            state.failures.pop_front();
        state.failures.push_back(now);
        if (state.failures.size() >= mLimits.failuresBeforeLockout)
        {
            state.failures.clear();
            state.lockedUntil = now + mLimits.lockoutDuration;
        }
    }

    void AuthenticationRateLimiter::recordSuccess(
        std::string_view account, std::string_view address)
    {
        if (!validIdentity(account, address))
            return;
        std::scoped_lock lock(mMutex);
        mFailures.erase(key(account, address));
    }

    std::size_t AuthenticationRateLimiter::trackedAccountAddresses() const
    {
        std::scoped_lock lock(mMutex);
        return mFailures.size();
    }

    std::size_t AuthenticationRateLimiter::trackedAddresses() const
    {
        std::scoped_lock lock(mMutex);
        return mAddresses.size();
    }
}
