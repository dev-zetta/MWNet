#include "RateLimits.hpp"

#include <algorithm>
#include <cmath>

namespace mwmp::protocol
{
    TokenBucket::TokenBucket(double refillPerSecond, double burst, Clock::time_point now) noexcept
        : mRefillPerSecond(std::max(0.0, refillPerSecond))
        , mBurst(std::max(0.0, burst))
        , mTokens(mBurst)
        , mLastRefill(now)
    {
    }

    void TokenBucket::refill(Clock::time_point now) noexcept
    {
        if (now <= mLastRefill)
            return;
        const double elapsed = std::chrono::duration<double>(now - mLastRefill).count();
        mTokens = std::min(mBurst, mTokens + elapsed * mRefillPerSecond);
        mLastRefill = now;
    }

    double TokenBucket::available(Clock::time_point now) noexcept
    {
        refill(now);
        return mTokens;
    }

    bool TokenBucket::consume(double amount, Clock::time_point now) noexcept
    {
        if (!std::isfinite(amount) || amount < 0.0)
            return false;
        refill(now);
        if (amount > mTokens)
            return false;
        mTokens -= amount;
        return true;
    }

    ConnectionRateLimiter::ConnectionRateLimiter(Clock::time_point now) noexcept
        : mMessages(limits::messagesPerSecond, limits::messageBurst, now)
        , mBytes(limits::bytesPerSecond, limits::byteBurst, now)
    {
    }

    bool ConnectionRateLimiter::consume(std::size_t bytes, Clock::time_point now) noexcept
    {
        const double byteCount = static_cast<double>(bytes);
        if (mMessages.available(now) < 1.0 || mBytes.available(now) < byteCount)
            return false;
        return mMessages.consume(1.0, now) && mBytes.consume(byteCount, now);
    }

    bool ChatRateLimiter::consume(Clock::time_point now) noexcept
    {
        while (!mMessages.empty() && now - mMessages.front() >= sWindow)
            mMessages.pop_front();
        if (mMessages.size() >= limits::chatMessagesPerWindow)
            return false;
        mMessages.push_back(now);
        return true;
    }
}
