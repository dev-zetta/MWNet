#ifndef OPENMW_MP_TRANSPORT_TRANSPORT_QUEUE_HPP
#define OPENMW_MP_TRANSPORT_TRANSPORT_QUEUE_HPP

#include "ITransport.hpp"

#include <condition_variable>
#include <cstddef>
#include <deque>
#include <mutex>
#include <optional>

namespace mwmp::transport
{
    class TransportQueue
    {
    public:
        explicit TransportQueue(std::size_t maximumMessages = 4096,
            std::size_t maximumBytes = 64U * 1024U * 1024U);

        bool tryPush(TransportEvent event);
        std::optional<TransportEvent> tryPop();
        std::optional<TransportEvent> waitPop(std::chrono::milliseconds timeout);
        void close();

        std::size_t size() const;
        std::size_t bytes() const;
        bool closed() const;

    private:
        static std::size_t eventBytes(const TransportEvent& event);
        std::optional<TransportEvent> popLocked();

        const std::size_t mMaximumMessages;
        const std::size_t mMaximumBytes;
        mutable std::mutex mMutex;
        std::condition_variable mChanged;
        std::deque<TransportEvent> mEvents;
        std::size_t mBytes = 0;
        bool mClosed = false;
    };
}

#endif
