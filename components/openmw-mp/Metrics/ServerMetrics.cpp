#include "ServerMetrics.hpp"

#include <algorithm>
#include <limits>
#include <vector>

namespace mwmp::metrics
{
    namespace
    {
        std::uint64_t saturatingAdd(std::uint64_t left,
            std::uint64_t right) noexcept
        {
            constexpr std::uint64_t maximum
                = std::numeric_limits<std::uint64_t>::max();
            return right > maximum - left ? maximum : left + right;
        }
    }

    void ServerMetrics::observeTick(std::chrono::nanoseconds duration) noexcept
    {
        std::scoped_lock lock(mMutex);
        addSample(mTickMicroseconds, duration);
    }

    void ServerMetrics::observeSerialization(
        std::chrono::nanoseconds duration) noexcept
    {
        std::scoped_lock lock(mMutex);
        addSample(mSerializationMicroseconds, duration);
    }

    void ServerMetrics::observeQueueDepth(std::size_t depth) noexcept
    {
        std::scoped_lock lock(mMutex);
        mQueueDepth = depth;
        mMaximumQueueDepth = std::max(mMaximumQueueDepth, depth);
    }

    void ServerMetrics::setResidentMemoryBytes(std::uint64_t bytes) noexcept
    {
        std::scoped_lock lock(mMutex);
        mResidentMemoryBytes = bytes;
    }

    void ServerMetrics::recordInbound(std::uint64_t connection,
        std::size_t bytes) noexcept
    {
        std::scoped_lock lock(mMutex);
        increment(mInbound, bytes);
        increment(mConnections[connection].inbound, bytes);
    }

    void ServerMetrics::recordOutbound(std::uint64_t connection,
        std::size_t bytes) noexcept
    {
        std::scoped_lock lock(mMutex);
        increment(mOutbound, bytes);
        increment(mConnections[connection].outbound, bytes);
    }

    void ServerMetrics::removeConnection(std::uint64_t connection) noexcept
    {
        std::scoped_lock lock(mMutex);
        mConnections.erase(connection);
    }

    ServerMetricsSnapshot ServerMetrics::snapshot() const
    {
        std::scoped_lock lock(mMutex);
        ServerMetricsSnapshot result;
        result.tickP99Microseconds = percentile99(mTickMicroseconds);
        result.serializationP99Microseconds
            = percentile99(mSerializationMicroseconds);
        result.queueDepth = mQueueDepth;
        result.maximumQueueDepth = mMaximumQueueDepth;
        result.residentMemoryBytes = mResidentMemoryBytes;
        result.inbound = mInbound;
        result.outbound = mOutbound;
        result.connections = mConnections;
        return result;
    }

    void ServerMetrics::addSample(std::deque<std::uint64_t>& samples,
        std::chrono::nanoseconds duration) noexcept
    {
        if (duration.count() < 0)
            return;
        const auto microseconds
            = std::chrono::duration_cast<std::chrono::microseconds>(duration);
        samples.push_back(static_cast<std::uint64_t>(microseconds.count()));
        if (samples.size() > MaximumLatencySamples)
            samples.pop_front();
    }

    std::uint64_t ServerMetrics::percentile99(
        const std::deque<std::uint64_t>& samples)
    {
        if (samples.empty())
            return 0;

        std::vector<std::uint64_t> sorted(samples.begin(), samples.end());
        std::ranges::sort(sorted);
        const std::size_t rank = (sorted.size() * 99 + 99) / 100;
        return sorted.at(std::max<std::size_t>(rank, 1) - 1);
    }

    void ServerMetrics::increment(TrafficCounters& counters,
        std::size_t bytes) noexcept
    {
        counters.messages = saturatingAdd(counters.messages, 1);
        counters.bytes = saturatingAdd(counters.bytes,
            static_cast<std::uint64_t>(bytes));
    }
}
