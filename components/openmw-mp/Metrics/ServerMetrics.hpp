#ifndef OPENMW_MP_METRICS_SERVER_METRICS_HPP
#define OPENMW_MP_METRICS_SERVER_METRICS_HPP

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <unordered_map>

namespace mwmp::metrics
{
    struct TrafficCounters
    {
        std::uint64_t messages = 0;
        std::uint64_t bytes = 0;
    };

    struct ConnectionTraffic
    {
        TrafficCounters inbound;
        TrafficCounters outbound;
    };

    struct ServerMetricsSnapshot
    {
        std::uint64_t tickP99Microseconds = 0;
        std::uint64_t serializationP99Microseconds = 0;
        std::size_t queueDepth = 0;
        std::size_t maximumQueueDepth = 0;
        std::uint64_t residentMemoryBytes = 0;
        TrafficCounters inbound;
        TrafficCounters outbound;
        std::unordered_map<std::uint64_t, ConnectionTraffic> connections;
    };

    class ServerMetrics
    {
    public:
        static constexpr std::size_t MaximumLatencySamples = 4096;

        void observeTick(std::chrono::nanoseconds duration) noexcept;
        void observeSerialization(std::chrono::nanoseconds duration) noexcept;
        void observeQueueDepth(std::size_t depth) noexcept;
        void setResidentMemoryBytes(std::uint64_t bytes) noexcept;
        void recordInbound(std::uint64_t connection, std::size_t bytes) noexcept;
        void recordOutbound(std::uint64_t connection, std::size_t bytes) noexcept;
        void removeConnection(std::uint64_t connection) noexcept;

        ServerMetricsSnapshot snapshot() const;

    private:
        static void addSample(std::deque<std::uint64_t>& samples,
            std::chrono::nanoseconds duration) noexcept;
        static std::uint64_t percentile99(
            const std::deque<std::uint64_t>& samples);
        static void increment(TrafficCounters& counters,
            std::size_t bytes) noexcept;

        mutable std::mutex mMutex;
        std::deque<std::uint64_t> mTickMicroseconds;
        std::deque<std::uint64_t> mSerializationMicroseconds;
        std::size_t mQueueDepth = 0;
        std::size_t mMaximumQueueDepth = 0;
        std::uint64_t mResidentMemoryBytes = 0;
        TrafficCounters mInbound;
        TrafficCounters mOutbound;
        std::unordered_map<std::uint64_t, ConnectionTraffic> mConnections;
    };
}

#endif
