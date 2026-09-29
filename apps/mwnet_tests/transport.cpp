#include <components/openmw-mp/Protocol/PacketCodec.hpp>
#include <components/openmw-mp/Protocol/ProtocolLimits.hpp>
#include <components/openmw-mp/Transport/ApplicationPacketBridge.hpp>
#include <components/openmw-mp/Transport/ApplicationPacketDispatcher.hpp>
#include <components/openmw-mp/Transport/ApplicationPacketReceiver.hpp>
#include <components/openmw-mp/Transport/SnapshotSequenceTracker.hpp>
#include <components/openmw-mp/Transport/TransportCodec.hpp>
#include <components/openmw-mp/Transport/TransportQueue.hpp>
#include <components/openmw-mp/Metrics/ServerMetrics.hpp>

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

    void testApplicationPacketBridge()
    {
        constexpr TransportConnectionId connection{ 17 };
        const std::vector<std::byte> body{ std::byte{ 1 }, std::byte{ 2 } };
        TransportMessage message;
        protocol::CodecError error = protocol::CodecError::InvalidValue;
        EXPECT(encodeApplicationPacket(protocol::ApplicationPacketId::PlayerPosition,
            ApplicationPacketFlow::ClientToServer, connection, 44, 9, body, message, error));
        EXPECT(error == protocol::CodecError::None);
        EXPECT(message.connection == connection);
        EXPECT(message.delivery == DeliveryMode::Unreliable);
        EXPECT(message.lane == MessageLane::Player);
        EXPECT(message.messageType
            == static_cast<std::uint16_t>(protocol::MessageType::MovementSnapshot));
        EXPECT(message.subject == 44);

        ApplicationPacket packet;
        EXPECT(static_cast<bool>(decodeApplicationPacket(
            message, ApplicationPacketFlow::ClientToServer, packet)));
        EXPECT(packet.id == protocol::ApplicationPacketId::PlayerPosition);
        EXPECT(packet.subject == 44);
        EXPECT(packet.sequence == 9);
        EXPECT(packet.payload == body);

        TransportMessage tampered = message;
        tampered.lane = MessageLane::Actor;
        packet.subject = 99;
        EXPECT(!decodeApplicationPacket(
            tampered, ApplicationPacketFlow::ClientToServer, packet));
        EXPECT(packet.subject == 99);

        EXPECT(!encodeApplicationPacket(protocol::ApplicationPacketId::PlayerPosition,
            ApplicationPacketFlow::ClientToServer, connection, 44, 0, body, message, error));
        EXPECT(error == protocol::CodecError::InvalidValue);

        ApplicationPacketRoute route;
        EXPECT(applicationPacketRoute(protocol::ApplicationPacketId::PlayerAttack,
            ApplicationPacketFlow::ClientToServer, route));
        EXPECT(route.messageType == protocol::MessageType::AttackIntent);
        EXPECT(applicationPacketRoute(protocol::ApplicationPacketId::PlayerAttack,
            ApplicationPacketFlow::ServerToClient, route));
        EXPECT(route.messageType == protocol::MessageType::CombatResult);
        EXPECT(applicationPacketRoute(protocol::ApplicationPacketId::ActorAuthority,
            ApplicationPacketFlow::ServerToClient, route));
        EXPECT(route.messageType == protocol::MessageType::AuthorityLease);
        EXPECT(applicationPacketRoute(protocol::ApplicationPacketId::Container,
            ApplicationPacketFlow::ClientToServer, route));
        EXPECT(route.messageType == protocol::MessageType::ContainerActionIntent);
        EXPECT(!applicationPacketRoute(protocol::ApplicationPacketId::UserMyId,
            ApplicationPacketFlow::ClientToServer, route));

        for (std::uint16_t value = protocol::firstApplicationPacketId + 1;
             value <= protocol::lastApplicationPacketId; ++value)
        {
            const auto id = static_cast<protocol::ApplicationPacketId>(value);
            EXPECT(applicationPacketRoute(id, ApplicationPacketFlow::ClientToServer, route));
            EXPECT(applicationPacketRoute(id, ApplicationPacketFlow::ServerToClient, route));
        }

        EXPECT(applicationPacketRoute(protocol::ApplicationPacketId::ClientScriptGlobal,
            ApplicationPacketFlow::ClientToServer, route));
        EXPECT(route.lane == MessageLane::Worldstate);
        EXPECT(applicationPacketRoute(protocol::ApplicationPacketId::ActorSpellsActive,
            ApplicationPacketFlow::ClientToServer, route));
        EXPECT(route.lane == MessageLane::Actor);
        EXPECT(applicationPacketRoute(protocol::ApplicationPacketId::PlayerCooldowns,
            ApplicationPacketFlow::ClientToServer, route));
        EXPECT(route.lane == MessageLane::Player);
    }

    void testEveryApplicationPacketRoundTrip()
    {
        constexpr TransportConnectionId connection{ 29 };
        const std::vector<std::byte> body{
            std::byte{ 0xa5 }, std::byte{ 0x5a }, std::byte{ 0x11 } };
        std::size_t routes = 0;
        for (std::uint16_t value = protocol::firstApplicationPacketId;
             value <= protocol::lastApplicationPacketId; ++value)
        {
            const auto id = static_cast<protocol::ApplicationPacketId>(value);
            for (const auto flow : { ApplicationPacketFlow::ClientToServer,
                     ApplicationPacketFlow::ServerToClient })
            {
                ApplicationPacketRoute route;
                if (!applicationPacketRoute(id, flow, route))
                    continue;
                ++routes;
                TransportMessage encoded;
                protocol::CodecError error = protocol::CodecError::InvalidValue;
                EXPECT(encodeApplicationPacket(id, flow, connection, 83, 7,
                    body, encoded, error));
                ApplicationPacket decoded;
                EXPECT(static_cast<bool>(decodeApplicationPacket(encoded, flow, decoded)));
                EXPECT(decoded.id == id);
                EXPECT(decoded.subject == 83);
                EXPECT(decoded.sequence == 7);
                EXPECT(decoded.payload == body);

                for (std::size_t size = 0; size < sizeof(std::uint16_t); ++size)
                {
                    TransportMessage truncated = encoded;
                    truncated.payload.resize(size);
                    ApplicationPacket unchanged;
                    unchanged.subject = 99;
                    EXPECT(!decodeApplicationPacket(truncated, flow, unchanged));
                    EXPECT(unchanged.subject == 99);
                }
            }
        }
        EXPECT(routes == 2U * (protocol::lastApplicationPacketId
            - protocol::firstApplicationPacketId));
    }

    class RecordingTransport final : public ITransport
    {
    public:
        bool listen(const ListenOptions&, TransportError&) override { return true; }
        bool connect(const ConnectOptions&, TransportConnectionId&, TransportError&) override
        {
            return true;
        }
        bool send(TransportMessage message, TransportError&) override
        {
            sent.push_back(std::move(message));
            return true;
        }
        std::optional<TransportEvent> poll(std::chrono::milliseconds) override
        {
            return std::nullopt;
        }
        void disconnect(TransportConnectionId) override {}
        void shutdown(std::chrono::milliseconds) override {}

        std::vector<TransportMessage> sent;
    };

    void testApplicationPacketDispatcher()
    {
        RecordingTransport clientTransport;
        ApplicationPacketDispatcher client(
            clientTransport, ApplicationPacketFlow::ClientToServer, 1);
        EXPECT(client.addConnection({ 8 }));
        EXPECT(client.addConnection({ 8 }));
        EXPECT(!client.addConnection({ 9 }));
        EXPECT(client.connectionCount() == 1);

        TransportError error;
        const std::vector<std::byte> body{ std::byte{ 7 } };
        EXPECT(client.sendToServer(protocol::ApplicationPacketId::PlayerPosition,
            55, body, error));
        EXPECT(clientTransport.sent.size() == 1);
        EXPECT(clientTransport.sent.front().connection == TransportConnectionId{ 8 });
        EXPECT(clientTransport.sent.front().sequence == 1);
        EXPECT(clientTransport.sent.front().delivery == DeliveryMode::Unreliable);
        EXPECT(!client.sendTo(protocol::ApplicationPacketId::ChatMessage,
            55, { 8 }, body, error));

        RecordingTransport serverTransport;
        mwmp::metrics::ServerMetrics metrics;
        ApplicationPacketDispatcher server(
            serverTransport, ApplicationPacketFlow::ServerToClient, 3, &metrics);
        EXPECT(server.addConnection({ 3 }));
        EXPECT(server.addConnection({ 1 }));
        EXPECT(server.addConnection({ 2 }));
        EXPECT(server.sendToAll(protocol::ApplicationPacketId::ChatMessage,
            77, body, error, TransportConnectionId{ 2 }));
        EXPECT(serverTransport.sent.size() == 2);
        EXPECT(serverTransport.sent[0].connection == TransportConnectionId{ 1 });
        EXPECT(serverTransport.sent[1].connection == TransportConnectionId{ 3 });
        EXPECT(serverTransport.sent[0].sequence < serverTransport.sent[1].sequence);
        const auto traffic = metrics.snapshot();
        EXPECT(traffic.outbound.messages == 2);
        EXPECT(traffic.outbound.bytes
            == 2 * (protocol::envelopeBytes + sizeof(std::uint16_t) + body.size()));
        EXPECT(server.sendTo(protocol::ApplicationPacketId::PlayerAttack,
            77, { 2 }, body, error));
        EXPECT(serverTransport.sent.back().messageType
            == static_cast<std::uint16_t>(protocol::MessageType::CombatResult));

        server.removeConnection({ 2 });
        EXPECT(!server.contains({ 2 }));
        EXPECT(!server.sendTo(protocol::ApplicationPacketId::ChatMessage,
            77, { 2 }, body, error));
        EXPECT(error.code == TransportErrorCode::Closed);
    }

    void testApplicationPacketReceiver()
    {
        constexpr TransportConnectionId connection{ 41 };
        const std::vector<std::byte> body{ std::byte{ 4 }, std::byte{ 2 } };
        TransportMessage message;
        protocol::CodecError error = protocol::CodecError::None;
        EXPECT(encodeApplicationPacket(protocol::ApplicationPacketId::PlayerPosition,
            ApplicationPacketFlow::ClientToServer, connection, 73, 8,
            body, message, error));

        ApplicationPacketReceiver receiver(ApplicationPacketFlow::ClientToServer, 2);
        ReceivedApplicationPacket packet;
        auto result = receiver.receive(message, packet);
        EXPECT(static_cast<bool>(result));
        EXPECT(result.status == ApplicationReceiveStatus::Accepted);
        EXPECT(packet.sender == connection);
        EXPECT(packet.id == protocol::ApplicationPacketId::PlayerPosition);
        EXPECT(packet.subject == connection.value);
        EXPECT(packet.sequence == 8);
        EXPECT(packet.payload == body);

        packet.subject = 999;
        result = receiver.receive(message, packet);
        EXPECT(!result);
        EXPECT(result.status == ApplicationReceiveStatus::StaleSnapshot);
        EXPECT(packet.subject == 999);

        message.sequence = 9;
        result = receiver.receive(message, packet);
        EXPECT(static_cast<bool>(result));
        EXPECT(packet.sequence == 9);

        TransportMessage invalid = message;
        invalid.messageType = static_cast<std::uint16_t>(protocol::MessageType::CombatResult);
        packet.subject = 999;
        result = receiver.receive(invalid, packet);
        EXPECT(!result);
        EXPECT(result.status == ApplicationReceiveStatus::Invalid);
        EXPECT(result.decode.error == protocol::CodecError::InvalidValue);
        EXPECT(packet.subject == 999);

        receiver.removeConnection(connection);
        message.sequence = 1;
        EXPECT(static_cast<bool>(receiver.receive(message, packet)));

        TransportMessage reliable;
        EXPECT(encodeApplicationPacket(protocol::ApplicationPacketId::ChatMessage,
            ApplicationPacketFlow::ClientToServer, connection, 73, 1,
            body, reliable, error));
        EXPECT(static_cast<bool>(receiver.receive(reliable, packet)));
        EXPECT(static_cast<bool>(receiver.receive(reliable, packet)));
        receiver.clear();

        TransportMessage actor;
        EXPECT(encodeApplicationPacket(protocol::ApplicationPacketId::ActorPosition,
            ApplicationPacketFlow::ClientToServer, connection, 901, 1,
            body, actor, error));
        EXPECT(static_cast<bool>(receiver.receive(actor, packet)));
        EXPECT(packet.subject == 901);
    }

}

int runTransportTests()
{
    testQueueBoundsAndAccounting();
    testSnapshotSequences();
    testTransportCodec();
    testApplicationPacketBridge();
    testEveryApplicationPacketRoundTrip();
    testApplicationPacketDispatcher();
    testApplicationPacketReceiver();
    return sFailures;
}
