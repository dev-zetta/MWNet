#include <components/openmw-mp/Metrics/ServerMetrics.hpp>

#include <chrono>
#include <iostream>

namespace
{
    int failures = 0;

    void expect(bool condition, const char* expression, int line)
    {
        if (condition)
            return;
        ++failures;
        std::cerr << "metrics.cpp:" << line << ": expectation failed: "
                  << expression << '\n';
    }

#define EXPECT(condition) expect((condition), #condition, __LINE__)

    void testLatencyPercentilesAreBounded()
    {
        mwmp::metrics::ServerMetrics metrics;
        metrics.observeTick(std::chrono::microseconds(-1));
        for (std::uint64_t value = 1;
             value <= mwmp::metrics::ServerMetrics::MaximumLatencySamples + 10;
             ++value)
        {
            metrics.observeTick(std::chrono::microseconds(value));
            metrics.observeSerialization(std::chrono::microseconds(value * 2));
        }

        const auto snapshot = metrics.snapshot();
        EXPECT(snapshot.tickP99Microseconds == 4066);
        EXPECT(snapshot.serializationP99Microseconds == 8132);
    }

    void testQueueAndMemoryGauges()
    {
        mwmp::metrics::ServerMetrics metrics;
        metrics.observeQueueDepth(7);
        metrics.observeQueueDepth(2);
        metrics.setResidentMemoryBytes(123456);

        const auto snapshot = metrics.snapshot();
        EXPECT(snapshot.queueDepth == 2);
        EXPECT(snapshot.maximumQueueDepth == 7);
        EXPECT(snapshot.residentMemoryBytes == 123456);
    }

    void testTrafficIsTrackedPerConnection()
    {
        mwmp::metrics::ServerMetrics metrics;
        metrics.recordInbound(42, 100);
        metrics.recordInbound(42, 50);
        metrics.recordOutbound(42, 25);
        metrics.recordOutbound(7, 75);

        auto snapshot = metrics.snapshot();
        EXPECT(snapshot.inbound.messages == 2);
        EXPECT(snapshot.inbound.bytes == 150);
        EXPECT(snapshot.outbound.messages == 2);
        EXPECT(snapshot.outbound.bytes == 100);
        EXPECT(snapshot.connections.at(42).inbound.bytes == 150);
        EXPECT(snapshot.connections.at(42).outbound.bytes == 25);
        EXPECT(snapshot.connections.at(7).outbound.bytes == 75);

        metrics.removeConnection(42);
        snapshot = metrics.snapshot();
        EXPECT(!snapshot.connections.contains(42));
        EXPECT(snapshot.inbound.bytes == 150);
    }
}

int runMetricsTests()
{
    testLatencyPercentilesAreBounded();
    testQueueAndMemoryGauges();
    testTrafficIsTrackedPerConnection();
    return failures;
}
