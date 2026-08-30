#ifndef OPENMW_MP_RATE_LIMITS_HPP
#define OPENMW_MP_RATE_LIMITS_HPP

#include "ProtocolLimits.hpp"

#include <chrono>
#include <cstddef>
#include <deque>

namespace mwmp::protocol
{
    class TokenBucket
    {
    public:
        using Clock = std::chrono::steady_clock;

        TokenBucket(double refillPerSecond, double burst, Clock::time_point now = Clock::now()) noexcept;
        bool consume(double amount, Clock::time_point now = Clock::now()) noexcept;
        double available(Clock::time_point now = Clock::now()) noexcept;

    private:
        void refill(Clock::time_point now) noexcept;

        double mRefillPerSecond;
        double mBurst;
        double mTokens;
        Clock::time_point mLastRefill;
    };

    class ConnectionRateLimiter
    {
    public:
        using Clock = TokenBucket::Clock;

        explicit ConnectionRateLimiter(Clock::time_point now = Clock::now()) noexcept;
        bool consume(std::size_t bytes, Clock::time_point now = Clock::now()) noexcept;

    private:
        TokenBucket mMessages;
        TokenBucket mBytes;
    };

    class ChatRateLimiter
    {
    public:
        using Clock = TokenBucket::Clock;

        bool consume(Clock::time_point now = Clock::now()) noexcept;

    private:
        static constexpr auto sWindow = std::chrono::seconds(10);
        std::deque<Clock::time_point> mMessages;
    };
}

#endif
