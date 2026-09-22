#include <components/openmw-mp/Transport/GameNetworkingSocketsTransport.hpp>
#include <components/openmw-mp/Transport/Protocol11Endpoint.hpp>
#include <components/openmw-mp/Transport/SecureTransport.hpp>

#include <components/openmw-mp/Security/ServerIdentity.hpp>
#include <components/openmw-mp/Security/TrustStore.hpp>

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
#include <string>

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

    void testSecureTransport()
    {
        const auto unique = std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count());
        const auto directory = std::filesystem::temp_directory_path()
            / ("tes3mp-secure-transport-" + unique);
        const auto identityPath = directory / "server-identity.key";
        const auto replacementIdentityPath = directory / "replacement-identity.key";
        const auto trustPath = directory / "trusted-servers.json";
        const auto automationTrustPath = directory / "automation-trusted-servers.json";
        std::string persistenceError;
        auto identity = mwmp::security::ServerIdentity::loadOrCreate(identityPath, persistenceError);
        auto trustStore = mwmp::security::TrustStore::load(trustPath, persistenceError);
        EXPECT(identity.has_value());
        EXPECT(trustStore.has_value());
        if (!identity || !trustStore)
            return;
        const std::string expectedFingerprint = identity->fingerprint();

        std::uint16_t selectedPort = 0;
        {
            SecureTransport server(std::make_unique<GameNetworkingSocketsTransport>(),
                std::move(*identity));
            SecureTransport client(std::make_unique<GameNetworkingSocketsTransport>(),
                std::move(*trustStore));
            TransportError error;

            ListenOptions listen;
            listen.address = "127.0.0.1";
            listen.maximumConnections = 2;
            listen.timeouts.handshake = 5s;
            listen.timeouts.read = 2s;
            bool listening = false;
            for (std::uint16_t port = 39100; port < 39130 && !listening; ++port)
            {
                listen.port = port;
                listening = server.listen(listen, error);
                if (listening)
                    selectedPort = port;
            }
            EXPECT(listening);
            EXPECT(server.serverFingerprint() == std::optional(expectedFingerprint));

            ConnectOptions connect;
            connect.host = "127.0.0.1";
            connect.port = selectedPort;
            connect.timeouts.handshake = 5s;
            connect.timeouts.read = 2s;
            TransportConnectionId clientConnection;
            EXPECT(client.connect(connect, clientConnection, error));

            std::optional<TransportConnectionId> serverConnection;
            bool clientAuthenticated = false;
            bool trustRequested = false;
            const auto deadline = std::chrono::steady_clock::now() + 5s;
            while (std::chrono::steady_clock::now() < deadline
                && (!clientAuthenticated || !serverConnection))
            {
                if (auto event = client.poll(5ms))
                {
                    if (event->type == TransportEventType::TrustRequired)
                    {
                        trustRequested = true;
                        EXPECT(event->detail == expectedFingerprint);

                        TransportMessage premature;
                        premature.connection = clientConnection;
                        premature.messageType = 7;
                        EXPECT(!client.send(premature, error));
                        EXPECT(error.code == TransportErrorCode::SecurityFailure);
                        EXPECT(client.confirmFingerprint(event->connection, event->detail, error));
                    }
                    else if (event->type == TransportEventType::Connected)
                        clientAuthenticated = true;
                }
                if (auto event = server.poll(5ms);
                    event && event->type == TransportEventType::Connected)
                    serverConnection = event->connection;
            }
            EXPECT(trustRequested);
            EXPECT(clientAuthenticated);
            EXPECT(serverConnection.has_value());

            // Menus and character creation can be idle beyond the raw read
            // deadline. Authenticated keepalives must remain transport-internal.
            const auto idleUntil = std::chrono::steady_clock::now() + 3s;
            while (std::chrono::steady_clock::now() < idleUntil)
            {
                EXPECT(!client.poll(5ms).has_value());
                EXPECT(!server.poll(5ms).has_value());
            }

            TransportMessage outbound;
            outbound.connection = clientConnection;
            outbound.delivery = DeliveryMode::Unreliable;
            outbound.lane = MessageLane::Player;
            outbound.messageType = 91;
            outbound.subject = 73;
            outbound.sequence = 12;
            outbound.payload = { std::byte{ 4 }, std::byte{ 5 } };
            EXPECT(client.send(outbound, error));

            std::optional<TransportEvent> received;
            const auto messageDeadline = std::chrono::steady_clock::now() + 5s;
            while (!received && std::chrono::steady_clock::now() < messageDeadline)
            {
                auto event = server.poll(20ms);
                if (event && event->type == TransportEventType::Message)
                    received = std::move(event);
            }
            EXPECT(received.has_value());
            if (received && serverConnection)
            {
                EXPECT(received->connection == *serverConnection);
                EXPECT(received->message.delivery == DeliveryMode::Unreliable);
                EXPECT(received->message.lane == MessageLane::Player);
                EXPECT(received->message.messageType == outbound.messageType);
                EXPECT(received->message.subject == outbound.subject);
                EXPECT(received->message.sequence == outbound.sequence);
                EXPECT(received->message.payload == outbound.payload);
            }

            // A peer that stops servicing its secure transport must still expire.
            bool idlePeerExpired = false;
            const auto expiryDeadline = std::chrono::steady_clock::now() + 3s;
            while (!idlePeerExpired && std::chrono::steady_clock::now() < expiryDeadline)
            {
                const auto event = server.poll(20ms);
                idlePeerExpired = event && event->type == TransportEventType::Disconnected;
            }
            EXPECT(idlePeerExpired);

            client.shutdown(1s);
            server.shutdown(1s);
        }

        auto automationIdentity = mwmp::security::ServerIdentity::loadOrCreate(
            identityPath, persistenceError);
        auto automationTrust = mwmp::security::TrustStore::load(
            automationTrustPath, persistenceError);
        EXPECT(automationIdentity.has_value());
        EXPECT(automationTrust.has_value());
        if (automationIdentity && automationTrust)
        {
            SecureTransport server(std::make_unique<GameNetworkingSocketsTransport>(),
                std::move(*automationIdentity));
            SecureTransport client(std::make_unique<GameNetworkingSocketsTransport>(),
                std::move(*automationTrust));
            TransportError error;
            ListenOptions listen;
            listen.address = "127.0.0.1";
            listen.port = selectedPort;
            listen.timeouts.handshake = 5s;
            listen.timeouts.read = 10s;
            EXPECT(server.listen(listen, error));

            ConnectOptions connect;
            connect.host = "127.0.0.1";
            connect.port = selectedPort;
            connect.trustedFingerprint = expectedFingerprint;
            connect.timeouts.handshake = 5s;
            connect.timeouts.read = 10s;
            TransportConnectionId connection;
            EXPECT(client.connect(connect, connection, error));

            bool clientAuthenticated = false;
            bool serverAuthenticated = false;
            bool prompted = false;
            const auto deadline = std::chrono::steady_clock::now() + 5s;
            while (std::chrono::steady_clock::now() < deadline
                && (!clientAuthenticated || !serverAuthenticated))
            {
                if (auto event = client.poll(5ms))
                {
                    prompted = prompted || event->type == TransportEventType::TrustRequired;
                    clientAuthenticated = clientAuthenticated
                        || event->type == TransportEventType::Connected;
                }
                if (auto event = server.poll(5ms))
                    serverAuthenticated = serverAuthenticated
                        || event->type == TransportEventType::Connected;
            }
            EXPECT(clientAuthenticated);
            EXPECT(serverAuthenticated);
            EXPECT(!prompted);
            client.shutdown(1s);
            server.shutdown(1s);

            auto storedAutomationTrust = mwmp::security::TrustStore::load(
                automationTrustPath, persistenceError);
            EXPECT(storedAutomationTrust.has_value());
            if (storedAutomationTrust)
                EXPECT(storedAutomationTrust->trustedFingerprint("127.0.0.1", selectedPort)
                    == std::optional(expectedFingerprint));
        }

        auto replacementIdentity = mwmp::security::ServerIdentity::loadOrCreate(
            replacementIdentityPath, persistenceError);
        auto reloadedTrust = mwmp::security::TrustStore::load(trustPath, persistenceError);
        EXPECT(replacementIdentity.has_value());
        EXPECT(reloadedTrust.has_value());
        if (replacementIdentity && reloadedTrust)
        {
            SecureTransport server(std::make_unique<GameNetworkingSocketsTransport>(),
                std::move(*replacementIdentity));
            SecureTransport client(std::make_unique<GameNetworkingSocketsTransport>(),
                std::move(*reloadedTrust));
            TransportError error;
            ListenOptions listen;
            listen.address = "127.0.0.1";
            listen.port = selectedPort;
            listen.timeouts.handshake = 5s;
            listen.timeouts.read = 10s;
            EXPECT(server.listen(listen, error));

            ConnectOptions connect;
            connect.host = "127.0.0.1";
            connect.port = selectedPort;
            connect.timeouts.handshake = 5s;
            connect.timeouts.read = 10s;
            TransportConnectionId connection;
            EXPECT(client.connect(connect, connection, error));

            bool mismatchBlocked = false;
            bool insecureFallback = false;
            const auto deadline = std::chrono::steady_clock::now() + 5s;
            while (std::chrono::steady_clock::now() < deadline && !mismatchBlocked)
            {
                if (auto event = client.poll(5ms))
                {
                    if (event->type == TransportEventType::Disconnected
                        && event->detail.find("fingerprint mismatch") != std::string::npos)
                        mismatchBlocked = true;
                    if (event->type == TransportEventType::TrustRequired
                        || event->type == TransportEventType::Connected)
                        insecureFallback = true;
                }
                (void)server.poll(5ms);
            }
            EXPECT(mismatchBlocked);
            EXPECT(!insecureFallback);
            client.shutdown(1s);
            server.shutdown(1s);
        }

        std::error_code cleanupError;
        std::filesystem::remove_all(directory, cleanupError);
    }

    void testProtocol11Endpoint()
    {
        const auto unique = std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count());
        const auto directory = std::filesystem::temp_directory_path()
            / ("tes3mp-protocol11-endpoint-" + unique);
        std::string persistenceError;
        auto server = Protocol11Endpoint::createServer(
            directory / "server-identity.key", persistenceError);
        auto client = Protocol11Endpoint::createClient(
            directory / "trusted-servers.json", persistenceError);
        EXPECT(server != nullptr);
        EXPECT(client != nullptr);
        if (!server || !client)
            return;

        TransportError error;
        ListenOptions listen;
        listen.address = "127.0.0.1";
        listen.maximumConnections = 2;
        listen.timeouts.handshake = 5s;
        listen.timeouts.read = 10s;
        bool listening = false;
        for (std::uint16_t port = 39130; port < 39160 && !listening; ++port)
        {
            listen.port = port;
            listening = server->listen(listen, error);
        }
        EXPECT(listening);
        const auto fingerprint = server->serverFingerprint();
        EXPECT(fingerprint.has_value());

        ConnectOptions connect;
        connect.host = listen.address;
        connect.port = listen.port;
        connect.trustedFingerprint = fingerprint;
        connect.timeouts = listen.timeouts;
        TransportConnectionId clientConnection;
        EXPECT(client->connect(connect, clientConnection, error));

        std::optional<TransportConnectionId> serverConnection;
        bool clientConnected = false;
        const auto deadline = std::chrono::steady_clock::now() + 5s;
        while (std::chrono::steady_clock::now() < deadline
            && (!clientConnected || !serverConnection))
        {
            if (auto event = client->poll(5ms))
                clientConnected = clientConnected
                    || event->type == TransportEventType::Connected;
            if (auto event = server->poll(5ms);
                event && event->type == TransportEventType::Connected)
                serverConnection = event->connection;
        }
        EXPECT(clientConnected);
        EXPECT(serverConnection.has_value());
        if (!clientConnected || !serverConnection)
            return;
        EXPECT(server->peerAddress(*serverConnection) == std::optional("127.0.0.1"));

        EXPECT(client->state(clientConnection)
            == std::optional(mwmp::session::State::TransportAuthenticated));
        EXPECT(server->state(*serverConnection)
            == std::optional(mwmp::session::State::TransportAuthenticated));

        TransportMessage gameplay;
        gameplay.connection = clientConnection;
        gameplay.lane = MessageLane::Player;
        gameplay.messageType = static_cast<std::uint16_t>(
            mwmp::protocol::MessageType::ChatIntent);
        EXPECT(!client->send(gameplay, error));
        EXPECT(error.code == TransportErrorCode::SecurityFailure);

        for (const auto state : { mwmp::session::State::ContentVerified,
                 mwmp::session::State::AccountAuthenticated,
                 mwmp::session::State::Spawned })
        {
            EXPECT(client->advance(clientConnection, state, error)
                == mwmp::session::TransitionResult::Advanced);
            EXPECT(server->advance(*serverConnection, state, error)
                == mwmp::session::TransitionResult::Advanced);
        }
        EXPECT(client->send(gameplay, error));

        bool received = false;
        const auto messageDeadline = std::chrono::steady_clock::now() + 5s;
        while (!received && std::chrono::steady_clock::now() < messageDeadline)
        {
            auto event = server->poll(20ms);
            received = event && event->type == TransportEventType::Message
                && event->message.messageType == gameplay.messageType;
        }
        EXPECT(received);

        client->shutdown(1s);
        server->shutdown(1s);
        std::error_code cleanupError;
        std::filesystem::remove_all(directory, cleanupError);
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
    if (serverConnected)
        EXPECT(server.peerAddress(serverConnected->connection) == std::optional("127.0.0.1"));

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
    const auto localDisconnected = client.poll(0ms);
    EXPECT(localDisconnected && localDisconnected->type == TransportEventType::Disconnected
        && localDisconnected->connection == clientConnection);
    EXPECT(!client.send(outbound, error));
    client.disconnect(clientConnection);
    EXPECT(!client.poll(0ms));
    EXPECT(waitFor(server, TransportEventType::Disconnected).has_value());

    // Reconnect on the same transports, then initiate closure from the server.
    // Both local and remote closure must retire state without destroying workers.
    EXPECT(client.connect(connect, clientConnection, error));
    EXPECT(waitFor(client, TransportEventType::Connected).has_value());
    const auto reconnected = waitFor(server, TransportEventType::Connected);
    EXPECT(reconnected.has_value());
    if (reconnected)
    {
        server.disconnect(reconnected->connection);
        const auto closed = server.poll(0ms);
        EXPECT(closed && closed->type == TransportEventType::Disconnected
            && closed->connection == reconnected->connection);
        outbound.connection = reconnected->connection;
        EXPECT(!server.send(outbound, error));
        server.disconnect(reconnected->connection);
        EXPECT(!server.poll(0ms));
        EXPECT(waitFor(client, TransportEventType::Disconnected).has_value());
    }
    client.shutdown(1s);
    server.shutdown(1s);
    testSecureTransport();
    testProtocol11Endpoint();
    return sFailures;
}
