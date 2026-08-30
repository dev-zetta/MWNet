#include <components/openmw-mp/Protocol/PacketCodec.hpp>
#include <components/openmw-mp/Protocol/ProtocolLimits.hpp>
#include <components/openmw-mp/Transport/SnapshotSequenceTracker.hpp>
#include <components/openmw-mp/Transport/TransportCodec.hpp>
#include <components/openmw-mp/Transport/TransportQueue.hpp>

#include <chrono>
#include <cstddef>
#include <iostream>
#include <utility>
#include <vector>

namespace
{
    using namespace mwmp;
    using namespace mwmp::transport;

    int sFailures = 0;

    void expect(bool condition, const char* expression, int line)
    {
        if (condition)
            return;
        std::cerr << "transport.cpp:" << line << ": expectation failed: " << expression << '\n';
        ++sFailures;
    }

#define EXPECT(condition) expect((condition), #condition, __LINE__)

    TransportEvent makeMessage(TransportConnectionId connection, std::size_t payloadBytes = 8)
    {
        TransportMessage message;
        message.connection = connection;
        message.delivery = DeliveryMode::Unreliable;
        message.lane = MessageLane::Player;
        message.messageType = 7;
        message.subject = 42;
        message.sequence = 3;
        message.payload.resize(payloadBytes);
        return { TransportEventType::Message, connection, std::move(message), {} };
    }

    void testQueueBoundsAndAccounting()
    {
        constexpr TransportConnectionId connection{ 1 };
        TransportQueue queue(2, protocol::envelopeBytes + 8);
        EXPECT(queue.tryPush(makeMessage(connection)));
        EXPECT(queue.size() == 1);
        EXPECT(queue.bytes() == protocol::envelopeBytes + 8);
        EXPECT(!queue.tryPush(makeMessage(connection, 1)));

        auto event = queue.tryPop();
        EXPECT(event.has_value());
        EXPECT(event->connection == connection);
        EXPECT(event->message.sequence == 3);
        EXPECT(queue.size() == 0);
        EXPECT(queue.bytes() == 0);

        TransportEvent mismatch = makeMessage(connection);
        mismatch.message.connection = TransportConnectionId{ 2 };
        EXPECT(!queue.tryPush(std::move(mismatch)));

        EXPECT(!queue.tryPush(makeMessage(connection, protocol::limits::normalMessageBytes + 1)));
        queue.close();
        EXPECT(queue.closed());
        EXPECT(!queue.tryPush(makeMessage(connection)));
        EXPECT(!queue.waitPop(std::chrono::milliseconds(0)).has_value());
    }

    void testSnapshotSequences()
    {
        constexpr TransportConnectionId first{ 1 };
        constexpr TransportConnectionId second{ 2 };
        SnapshotSequenceTracker tracker(3);

        EXPECT(tracker.accept(first, MessageLane::Player, 10, 0));
        EXPECT(!tracker.accept(first, MessageLane::Player, 10, 0));
        EXPECT(tracker.accept(first, MessageLane::Player, 10, 1));
        EXPECT(!tracker.accept(first, MessageLane::Player, 10, 1));
        EXPECT(tracker.accept(first, MessageLane::Actor, 10, 1));
        EXPECT(tracker.accept(second, MessageLane::Player, 10, 1));
        EXPECT(!tracker.accept(second, MessageLane::Player, 11, 1));
        EXPECT(tracker.size() == 3);

        tracker.erase(first);
        EXPECT(tracker.size() == 1);
        EXPECT(tracker.accept(second, MessageLane::Player, 11, 1));
        tracker.clear();
        EXPECT(tracker.size() == 0);
    }

    void testTransportCodec()
    {
        constexpr TransportConnectionId connection{ 7 };
        TransportMessage original;
        original.connection = connection;
        original.delivery = DeliveryMode::Unreliable;
        original.lane = MessageLane::Actor;
        original.messageType = 81;
        original.flags = protocol::envelopeFlagBulkChunk;
        original.subject = 99;
        original.sequence = 123;
        original.payload = { std::byte{ 1 }, std::byte{ 2 }, std::byte{ 3 } };

        std::vector<std::byte> encoded;
        protocol::CodecError error = protocol::CodecError::InvalidValue;
        EXPECT(encodeTransportMessage(original, encoded, error));
        EXPECT(error == protocol::CodecError::None);

        TransportMessage decoded;
        const protocol::DecodeResult result = decodeTransportMessage(
            encoded, connection, MessageLane::Actor, DeliveryMode::Unreliable, decoded);
        EXPECT(static_cast<bool>(result));
        EXPECT(decoded.connection == connection);
        EXPECT(decoded.delivery == DeliveryMode::Unreliable);
        EXPECT(decoded.lane == MessageLane::Actor);
        EXPECT(decoded.messageType == original.messageType);
        EXPECT(decoded.subject == original.subject);
        EXPECT(decoded.sequence == original.sequence);
        EXPECT(decoded.payload == original.payload);

        TransportMessage unchanged;
        unchanged.messageType = 5;
        EXPECT(!decodeTransportMessage(
            encoded, connection, MessageLane::Actor, DeliveryMode::ReliableOrdered, unchanged));
        EXPECT(unchanged.messageType == 5);
    }
}

int runTransportTests()
{
    testQueueBoundsAndAccounting();
    testSnapshotSequences();
    testTransportCodec();
    return sFailures;
}
