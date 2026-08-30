#include "TransportQueue.hpp"

#include <components/openmw-mp/Protocol/PacketCodec.hpp>
#include <components/openmw-mp/Protocol/ProtocolLimits.hpp>

#include <utility>

namespace mwmp::transport
{
    TransportQueue::TransportQueue(std::size_t maximumMessages, std::size_t maximumBytes)
        : mMaximumMessages(maximumMessages)
        , mMaximumBytes(maximumBytes)
    {
    }

    bool TransportQueue::tryPush(TransportEvent event)
    {
        const std::size_t bytes = eventBytes(event);
        if (!event.connection
            || (event.type == TransportEventType::Message
                && (!event.message.connection || event.message.connection != event.connection)))
            return false;
        if (event.type == TransportEventType::Message)
        {
            const bool bulk = (event.message.flags & protocol::envelopeFlagBulkChunk) != 0;
            const std::size_t limit = bulk ? protocol::limits::bulkChunkBytes : protocol::limits::normalMessageBytes;
            if (event.message.payload.size() > limit)
                return false;
        }

        std::unique_lock lock(mMutex);
        if (mClosed || mEvents.size() >= mMaximumMessages || bytes > mMaximumBytes - mBytes)
            return false;

        mBytes += bytes;
        mEvents.push_back(std::move(event));
        lock.unlock();
        mChanged.notify_one();
        return true;
    }

    std::optional<TransportEvent> TransportQueue::tryPop()
    {
        std::scoped_lock lock(mMutex);
        return popLocked();
    }

    std::optional<TransportEvent> TransportQueue::waitPop(std::chrono::milliseconds timeout)
    {
        std::unique_lock lock(mMutex);
        mChanged.wait_for(lock, timeout, [this] { return mClosed || !mEvents.empty(); });
        return popLocked();
    }

    void TransportQueue::close()
    {
        std::unique_lock lock(mMutex);
        mClosed = true;
        lock.unlock();
        mChanged.notify_all();
    }

    std::size_t TransportQueue::size() const
    {
        std::scoped_lock lock(mMutex);
        return mEvents.size();
    }

    std::size_t TransportQueue::bytes() const
    {
        std::scoped_lock lock(mMutex);
        return mBytes;
    }

    bool TransportQueue::closed() const
    {
        std::scoped_lock lock(mMutex);
        return mClosed;
    }

    std::size_t TransportQueue::eventBytes(const TransportEvent& event)
    {
        if (event.type != TransportEventType::Message)
            return 0;
        return protocol::envelopeBytes + event.message.payload.size();
    }

    std::optional<TransportEvent> TransportQueue::popLocked()
    {
        if (mEvents.empty())
            return std::nullopt;

        TransportEvent event = std::move(mEvents.front());
        mEvents.pop_front();
        mBytes -= eventBytes(event);
        return event;
    }
}
