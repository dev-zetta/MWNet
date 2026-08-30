#include <components/openmw-mp/Transport/GameNetworkingSocketsTransport.hpp>

#include <chrono>
#include <cstddef>
#include <iostream>
#include <optional>

namespace
{
    using namespace mwmp::transport;
    using namespace std::chrono_literals;

    int sFailures = 0;

    void expect(bool condition, const char* expression, int line)
    {
        if (condition)
            return;
        std::cerr << "gns_transport.cpp:" << line << ": expectation failed: " << expression << '\n';
        ++sFailures;
    }

#define EXPECT(condition) expect((condition), #condition, __LINE__)

    std::optional<TransportEvent> waitFor(
        GameNetworkingSocketsTransport& transport, TransportEventType type)
    {
        const auto deadline = std::chrono::steady_clock::now() + 5s;
        while (std::chrono::steady_clock::now() < deadline)
        {
            auto event = transport.poll(50ms);
            if (event && event->type == type)
                return event;
        }
        return std::nullopt;
    }
}

int runGameNetworkingSocketsTests()
{
    GameNetworkingSocketsTransport server;
    GameNetworkingSocketsTransport client;
    TransportError error;

    ListenOptions listen;
    listen.address = "127.0.0.1";
    listen.maximumConnections = 2;
    listen.timeouts.read = 5s;
    bool listening = false;
    for (std::uint16_t port = 39070; port < 39100 && !listening; ++port)
    {
        listen.port = port;
        listening = server.listen(listen, error);
    }
    EXPECT(listening);
    EXPECT(!error);
    EXPECT(server.boundPort().has_value());

    ConnectOptions connect;
    connect.host = "127.0.0.1";
    connect.port = server.boundPort().value_or(0);
    connect.timeouts.read = 5s;
    TransportConnectionId clientConnection;
    EXPECT(client.connect(connect, clientConnection, error));
    EXPECT(static_cast<bool>(clientConnection));

    const auto clientConnected = waitFor(client, TransportEventType::Connected);
    const auto serverConnected = waitFor(server, TransportEventType::Connected);
    EXPECT(clientConnected.has_value());
    EXPECT(serverConnected.has_value());

    TransportMessage outbound;
    outbound.connection = clientConnection;
    outbound.delivery = DeliveryMode::ReliableOrdered;
    outbound.lane = MessageLane::Actor;
    outbound.messageType = 77;
    outbound.subject = 42;
    outbound.sequence = 9;
    outbound.payload = { std::byte{ 1 }, std::byte{ 2 }, std::byte{ 3 } };
    EXPECT(client.send(outbound, error));

    const auto received = waitFor(server, TransportEventType::Message);
    EXPECT(received.has_value());
    if (received && serverConnected)
    {
        EXPECT(received->message.connection == serverConnected->connection);
        EXPECT(received->message.delivery == DeliveryMode::ReliableOrdered);
        EXPECT(received->message.lane == MessageLane::Actor);
        EXPECT(received->message.messageType == 77);
        EXPECT(received->message.subject == 42);
        EXPECT(received->message.sequence == 9);
        EXPECT(received->message.payload == outbound.payload);
    }

    client.disconnect(clientConnection);
    EXPECT(waitFor(server, TransportEventType::Disconnected).has_value());
    client.shutdown(1s);
    server.shutdown(1s);
    return sFailures;
}
