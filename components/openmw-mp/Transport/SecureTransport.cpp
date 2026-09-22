#include "SecureTransport.hpp"

#include "TransportCodec.hpp"
#include "TransportQueue.hpp"

#include <components/openmw-mp/Protocol/PacketCodec.hpp>
#include <components/openmw-mp/Protocol/ProtocolLimits.hpp>
#include <components/openmw-mp/Security/SecureSession.hpp>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace mwmp::transport
{
    namespace
    {
        using Clock = std::chrono::steady_clock;

        constexpr std::uint16_t sClientHelloMessage = 0xfff0;
        constexpr std::uint16_t sServerHelloMessage = 0xfff1;
        constexpr std::uint16_t sClientProofMessage = 0xfff2;
        constexpr std::uint16_t sServerProofMessage = 0xfff3;
        constexpr std::uint16_t sSecureDataMessage = 0xfff4;
        constexpr std::uint16_t sKeepaliveMessage = 0xfff5;
        constexpr std::string_view sKeepalive = "TES3MP protocol 11 keepalive";
        constexpr std::string_view sClientProof = "TES3MP protocol 11 client proof";
        constexpr std::string_view sServerProof = "TES3MP protocol 11 server proof";
        constexpr std::size_t sSecureFrameOverhead = 1 + sizeof(std::uint64_t)
            + crypto_aead_xchacha20poly1305_ietf_ABYTES;

        bool isReservedMessage(std::uint16_t messageType)
        {
            return messageType >= sClientHelloMessage && messageType <= sKeepaliveMessage;
        }

        std::span<const std::byte> bytes(std::string_view value)
        {
            return std::as_bytes(std::span(value));
        }
    }

    struct SecureTransport::Impl
    {
        enum class Role
        {
            Server,
            Client,
        };

        enum class Phase
        {
            RawConnected,
            AwaitingClientHello,
            AwaitingServerHello,
            AwaitingTrust,
            AwaitingClientProof,
            AwaitingServerProof,
            Authenticated,
        };

        struct Connection
        {
            Phase phase = Phase::RawConnected;
            Clock::time_point handshakeDeadline;
            Clock::time_point lastSent = Clock::now();
            std::unique_ptr<security::ClientHandshake> clientHandshake;
            security::SecureSession session;
            std::string presentedFingerprint;
        };

        Impl(std::unique_ptr<ITransport> value, security::ServerIdentity identityValue)
            : role(Role::Server)
            , transport(std::move(value))
            , identity(std::move(identityValue))
        {
        }

        Impl(std::unique_ptr<ITransport> value, security::TrustStore trustStoreValue)
            : role(Role::Client)
            , transport(std::move(value))
            , trustStore(std::move(trustStoreValue))
        {
        }

        Role role;
        std::unique_ptr<ITransport> transport;
        std::optional<security::ServerIdentity> identity;
        std::optional<security::TrustStore> trustStore;
        std::string remoteHost;
        std::uint16_t remotePort = 0;
        TransportConnectionId pendingOutboundConnection;
        bool connectInProgress = false;
        std::optional<std::string> automationFingerprint;
        std::chrono::milliseconds handshakeTimeout{ 10'000 };
        std::chrono::milliseconds keepaliveInterval{ 5'000 };
        mutable std::mutex mutex;
        std::unordered_map<std::uint64_t, Connection> connections;
        TransportQueue events;

        bool listen(const ListenOptions& options, TransportError& error)
        {
            if (role != Role::Server || !identity)
            {
                error = { TransportErrorCode::InvalidConfiguration,
                    "a client secure transport cannot listen" };
                return false;
            }
            handshakeTimeout = options.timeouts.handshake;
            keepaliveInterval = std::clamp(options.timeouts.read / 3,
                std::chrono::milliseconds(1), std::chrono::milliseconds(5'000));
            return transport->listen(options, error);
        }

        bool connect(const ConnectOptions& options, TransportConnectionId& connection,
            TransportError& error)
        {
            if (role != Role::Client || !trustStore)
            {
                error = { TransportErrorCode::InvalidConfiguration,
                    "a server secure transport cannot initiate connections" };
                return false;
            }
            std::scoped_lock lock(mutex);
            if (!connections.empty() || connectInProgress || pendingOutboundConnection)
            {
                error = { TransportErrorCode::InvalidConfiguration,
                    "secure client transport already has an active connection" };
                return false;
            }
            if (!remoteHost.empty() && (remoteHost != options.host || remotePort != options.port))
            {
                error = { TransportErrorCode::InvalidConfiguration,
                    "one secure client transport may connect to only one endpoint" };
                return false;
            }
            if (options.trustedFingerprint
                && !security::TrustStore::isValidFingerprint(*options.trustedFingerprint))
            {
                error = { TransportErrorCode::InvalidConfiguration,
                    "--trust-fingerprint is not a valid Ed25519 fingerprint" };
                return false;
            }
            remoteHost = options.host;
            remotePort = options.port;
            automationFingerprint = options.trustedFingerprint;
            handshakeTimeout = options.timeouts.handshake;
            keepaliveInterval = std::clamp(options.timeouts.read / 3,
                std::chrono::milliseconds(1), std::chrono::milliseconds(5'000));
            connectInProgress = true;
            if (!transport->connect(options, connection, error))
            {
                connectInProgress = false;
                return false;
            }
            pendingOutboundConnection = connection;
            connectInProgress = false;
            return true;
        }

        bool send(TransportMessage message, TransportError& error)
        {
            if (isReservedMessage(message.messageType))
            {
                error = { TransportErrorCode::MessageRejected,
                    "application attempted to use a reserved secure-transport message type" };
                return false;
            }

            std::scoped_lock lock(mutex);
            const auto found = connections.find(message.connection.value);
            if (found == connections.end() || found->second.phase != Phase::Authenticated)
            {
                error = { TransportErrorCode::SecurityFailure,
                    "connection has not completed transport authentication" };
                return false;
            }

            std::vector<std::byte> encoded;
            protocol::CodecError codecError = protocol::CodecError::None;
            if (!encodeTransportMessage(message, encoded, codecError))
            {
                error = { TransportErrorCode::MessageRejected,
                    "failed to encode the authenticated protocol message" };
                return false;
            }

            const std::size_t wireLimit = (message.flags & protocol::envelopeFlagBulkChunk) != 0
                ? protocol::limits::bulkChunkBytes
                : protocol::limits::normalMessageBytes;
            if (encoded.size() + sizeof(std::uint8_t) + sSecureFrameOverhead > wireLimit)
            {
                error = { TransportErrorCode::MessageRejected,
                    "authenticated message exceeds the protocol-11 wire limit" };
                return false;
            }

            protocol::PacketWriter authenticatedWriter(wireLimit - sSecureFrameOverhead);
            authenticatedWriter.writeU8(static_cast<std::uint8_t>(message.lane));
            authenticatedWriter.writeBytes(encoded);
            if (!authenticatedWriter.valid())
            {
                error = { TransportErrorCode::MessageRejected,
                    "failed to encode authenticated transport metadata" };
                return false;
            }
            auto authenticatedMessage = authenticatedWriter.take();

            std::vector<std::byte> encrypted;
            security::SecurityError securityError = security::SecurityError::None;
            if (!found->second.session.seal(authenticatedMessage, encrypted, securityError))
            {
                error = { TransportErrorCode::SecurityFailure,
                    "failed to encrypt the protocol-11 message" };
                return false;
            }

            TransportMessage outer;
            outer.connection = message.connection;
            outer.delivery = message.delivery;
            outer.lane = message.lane;
            outer.messageType = sSecureDataMessage;
            outer.flags = message.flags & protocol::envelopeFlagBulkChunk;
            outer.sequence = message.sequence;
            outer.payload = std::move(encrypted);
            const bool sent = transport->send(std::move(outer), error);
            if (sent)
                found->second.lastSent = Clock::now();
            return sent;
        }

        std::optional<TransportEvent> poll(std::chrono::milliseconds timeout)
        {
            sendKeepalives();
            if (auto ready = events.tryPop())
                return ready;

            const auto deadline = Clock::now() + timeout;
            do
            {
                enforceHandshakeDeadlines();
                if (auto ready = events.tryPop())
                    return ready;

                const auto now = Clock::now();
                const auto remaining = now >= deadline ? std::chrono::milliseconds(0)
                                                       : std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now);
                auto event = transport->poll(std::min(remaining, keepaliveInterval));
                if (!event)
                    return events.tryPop();
                process(std::move(*event));
                if (auto ready = events.tryPop())
                    return ready;
            } while (Clock::now() < deadline);
            return std::nullopt;
        }

        void process(TransportEvent event)
        {
            if (event.type == TransportEventType::Connected)
                rawConnected(event.connection);
            else if (event.type == TransportEventType::Disconnected)
            {
                bool existed = false;
                {
                    std::scoped_lock lock(mutex);
                    existed = connections.erase(event.connection.value) != 0;
                    if (role == Role::Client && event.connection == pendingOutboundConnection)
                    {
                        pendingOutboundConnection = {};
                        existed = true;
                    }
                }
                if (existed)
                    events.tryPush(std::move(event));
            }
            else if (event.type == TransportEventType::Message)
                rawMessage(std::move(event.message));
        }

        void rawConnected(TransportConnectionId connectionId)
        {
            Connection connection;
            connection.handshakeDeadline = Clock::now() + handshakeTimeout;
            if (role == Role::Server)
                connection.phase = Phase::AwaitingClientHello;
            else
            {
                connection.phase = Phase::AwaitingServerHello;
                connection.clientHandshake = std::make_unique<security::ClientHandshake>();
                if (!*connection.clientHandshake)
                {
                    fail(connectionId, "libsodium initialization failed");
                    return;
                }
            }

            {
                std::scoped_lock lock(mutex);
                if (role == Role::Client)
                {
                    if (connectionId != pendingOutboundConnection)
                    {
                        transport->disconnect(connectionId);
                        return;
                    }
                    pendingOutboundConnection = {};
                }
                if (!connections.emplace(connectionId.value, std::move(connection)).second)
                {
                    transport->disconnect(connectionId);
                    return;
                }
            }

            if (role == Role::Client)
            {
                std::vector<std::byte> encoded;
                {
                    std::scoped_lock lock(mutex);
                    security::encodeClientHello(
                        connections.at(connectionId.value).clientHandshake->hello(), encoded);
                }
                if (!sendControl(connectionId, sClientHelloMessage, std::move(encoded)))
                    fail(connectionId, "failed to send the client identity handshake");
            }
        }

        void rawMessage(TransportMessage message)
        {
            std::scoped_lock lock(mutex);
            const auto found = connections.find(message.connection.value);
            if (found == connections.end())
            {
                transport->disconnect(message.connection);
                return;
            }

            Connection& connection = found->second;
            if (role == Role::Server && connection.phase == Phase::AwaitingClientHello
                && message.messageType == sClientHelloMessage)
                acceptClientHello(message, connection);
            else if (role == Role::Client && connection.phase == Phase::AwaitingServerHello
                && message.messageType == sServerHelloMessage)
                acceptServerHello(message, connection);
            else if (role == Role::Server && connection.phase == Phase::AwaitingClientProof
                && message.messageType == sClientProofMessage)
                acceptClientProof(message, connection);
            else if (role == Role::Client && connection.phase == Phase::AwaitingServerProof
                && message.messageType == sServerProofMessage)
                acceptServerProof(message, connection);
            else if (connection.phase == Phase::Authenticated
                && message.messageType == sSecureDataMessage)
                acceptSecureData(message, connection);
            else if (connection.phase == Phase::Authenticated
                && message.messageType == sKeepaliveMessage)
                acceptKeepalive(message, connection);
            else
                failLocked(message.connection, "unexpected or out-of-order secure transport message");
        }

        void acceptClientHello(const TransportMessage& message, Connection& connection)
        {
            security::ClientHello hello;
            if (message.delivery != DeliveryMode::ReliableOrdered || message.lane != MessageLane::System
                || !security::decodeClientHello(message.payload, hello))
            {
                failLocked(message.connection, "invalid client identity handshake");
                return;
            }

            security::ServerHello response;
            security::ServerHandshake handshake(*identity);
            security::SecurityError error = security::SecurityError::None;
            if (!handshake.accept(hello, response, connection.session, error))
            {
                failLocked(message.connection, "server identity key exchange failed");
                return;
            }
            std::vector<std::byte> encoded;
            if (!security::encodeServerHello(response, encoded)
                || !sendControl(message.connection, sServerHelloMessage, std::move(encoded)))
            {
                failLocked(message.connection, "failed to send the server identity handshake");
                return;
            }
            connection.phase = Phase::AwaitingClientProof;
        }

        void acceptServerHello(const TransportMessage& message, Connection& connection)
        {
            security::ServerHello response;
            if (message.delivery != DeliveryMode::ReliableOrdered || message.lane != MessageLane::System
                || !security::decodeServerHello(message.payload, response))
            {
                failLocked(message.connection, "invalid server identity handshake");
                return;
            }

            security::SecurityError error = security::SecurityError::None;
            if (!connection.clientHandshake->finish(response, connection.session, error))
            {
                failLocked(message.connection, "server identity signature verification failed");
                return;
            }
            connection.clientHandshake.reset();
            connection.presentedFingerprint = security::fingerprint(response.identityPublicKey);

            if (automationFingerprint)
            {
                if (*automationFingerprint != connection.presentedFingerprint)
                {
                    failLocked(message.connection, "server fingerprint differs from --trust-fingerprint");
                    return;
                }
                std::string trustError;
                if (!trustStore->trust(remoteHost, remotePort, connection.presentedFingerprint, trustError))
                {
                    failLocked(message.connection, trustError);
                    return;
                }
                (void)sendClientProofLocked(message.connection, connection);
                return;
            }

            switch (trustStore->assess(remoteHost, remotePort, connection.presentedFingerprint))
            {
                case security::TrustDecision::Trusted:
                    (void)sendClientProofLocked(message.connection, connection);
                    break;
                case security::TrustDecision::ConfirmationRequired:
                    connection.phase = Phase::AwaitingTrust;
                    if (!events.tryPush({ TransportEventType::TrustRequired, message.connection, {},
                            connection.presentedFingerprint }))
                        failLocked(message.connection, "secure transport event queue is full");
                    break;
                case security::TrustDecision::FingerprintMismatch:
                    failLocked(message.connection, "stored server fingerprint mismatch");
                    break;
                default:
                    failLocked(message.connection, "invalid server identity or trust-store endpoint");
                    break;
            }
        }

        bool sendClientProofLocked(TransportConnectionId connectionId, Connection& connection)
        {
            std::vector<std::byte> encrypted;
            security::SecurityError error = security::SecurityError::None;
            if (!connection.session.seal(bytes(sClientProof), encrypted, error)
                || !sendControl(connectionId, sClientProofMessage, std::move(encrypted)))
            {
                failLocked(connectionId, "failed to send encrypted client proof");
                return false;
            }
            connection.phase = Phase::AwaitingServerProof;
            return true;
        }

        void acceptClientProof(const TransportMessage& message, Connection& connection)
        {
            std::vector<std::byte> plaintext;
            security::SecurityError error = security::SecurityError::None;
            if (!connection.session.open(message.payload, plaintext, error)
                || !std::ranges::equal(plaintext, bytes(sClientProof)))
            {
                failLocked(message.connection, "encrypted client proof was invalid");
                return;
            }

            std::vector<std::byte> encrypted;
            if (!connection.session.seal(bytes(sServerProof), encrypted, error)
                || !sendControl(message.connection, sServerProofMessage, std::move(encrypted)))
            {
                failLocked(message.connection, "failed to send encrypted server proof");
                return;
            }
            connection.phase = Phase::Authenticated;
            if (!events.tryPush(
                    { TransportEventType::Connected, message.connection, {}, identity->fingerprint() }))
                failLocked(message.connection, "secure transport event queue is full");
        }

        void acceptServerProof(const TransportMessage& message, Connection& connection)
        {
            std::vector<std::byte> plaintext;
            security::SecurityError error = security::SecurityError::None;
            if (!connection.session.open(message.payload, plaintext, error)
                || !std::ranges::equal(plaintext, bytes(sServerProof)))
            {
                failLocked(message.connection, "encrypted server proof was invalid");
                return;
            }
            connection.phase = Phase::Authenticated;
            if (!events.tryPush({ TransportEventType::Connected, message.connection, {},
                    connection.presentedFingerprint }))
                failLocked(message.connection, "secure transport event queue is full");
        }

        void acceptSecureData(const TransportMessage& message, Connection& connection)
        {
            std::vector<std::byte> plaintext;
            security::SecurityError error = security::SecurityError::None;
            if (!connection.session.open(message.payload, plaintext, error))
            {
                failLocked(message.connection, "authenticated message verification failed");
                return;
            }
            if (plaintext.empty()
                || std::to_integer<std::uint8_t>(plaintext.front())
                    != static_cast<std::uint8_t>(message.lane))
            {
                failLocked(message.connection, "authenticated message lane did not match its wire lane");
                return;
            }

            TransportMessage decoded;
            if (!decodeTransportMessage(std::span(plaintext).subspan(1), message.connection, message.lane,
                    message.delivery, decoded))
            {
                failLocked(message.connection, "decrypted protocol-11 message was invalid");
                return;
            }
            if (!events.tryPush(
                    { TransportEventType::Message, message.connection, std::move(decoded), {} }))
                failLocked(message.connection, "secure transport event queue is full");
        }

        void acceptKeepalive(const TransportMessage& message, Connection& connection)
        {
            std::vector<std::byte> plaintext;
            security::SecurityError error = security::SecurityError::None;
            if (message.lane != MessageLane::System
                || message.delivery != DeliveryMode::ReliableOrdered
                || !connection.session.open(message.payload, plaintext, error)
                || !std::ranges::equal(plaintext, bytes(sKeepalive)))
                failLocked(message.connection, "invalid authenticated keepalive");
        }

        void sendKeepalives()
        {
            std::scoped_lock lock(mutex);
            const auto now = Clock::now();
            std::vector<TransportConnectionId> failed;
            for (auto& [id, connection] : connections)
            {
                if (connection.phase != Phase::Authenticated
                    || now - connection.lastSent < keepaliveInterval)
                    continue;
                std::vector<std::byte> encrypted;
                security::SecurityError error = security::SecurityError::None;
                if (!connection.session.seal(bytes(sKeepalive), encrypted, error)
                    || !sendControl(TransportConnectionId(id), sKeepaliveMessage, std::move(encrypted)))
                    failed.emplace_back(id);
                else
                    connection.lastSent = now;
            }
            for (auto id : failed)
                failLocked(id, "failed to send authenticated keepalive");
        }

        bool sendControl(TransportConnectionId connection, std::uint16_t messageType,
            std::vector<std::byte> payload)
        {
            TransportMessage message;
            message.connection = connection;
            message.delivery = DeliveryMode::ReliableOrdered;
            message.lane = MessageLane::System;
            message.messageType = messageType;
            message.payload = std::move(payload);
            TransportError error;
            return transport->send(std::move(message), error);
        }

        bool confirmFingerprint(TransportConnectionId connectionId,
            std::string_view fingerprint, TransportError& error)
        {
            std::scoped_lock lock(mutex);
            const auto found = connections.find(connectionId.value);
            if (role != Role::Client || found == connections.end()
                || found->second.phase != Phase::AwaitingTrust)
            {
                error = { TransportErrorCode::InvalidConfiguration,
                    "connection is not waiting for a fingerprint decision" };
                return false;
            }
            if (fingerprint != found->second.presentedFingerprint)
            {
                error = { TransportErrorCode::SecurityFailure,
                    "confirmed fingerprint differs from the presented fingerprint" };
                return false;
            }
            std::string trustError;
            if (!trustStore->trust(remoteHost, remotePort, fingerprint, trustError))
            {
                error = { TransportErrorCode::SecurityFailure, std::move(trustError) };
                return false;
            }
            if (!sendClientProofLocked(connectionId, found->second))
            {
                error = { TransportErrorCode::SecurityFailure,
                    "failed to continue the confirmed secure handshake" };
                return false;
            }
            return true;
        }

        void enforceHandshakeDeadlines()
        {
            const auto now = Clock::now();
            std::vector<TransportConnectionId> expired;
            {
                std::scoped_lock lock(mutex);
                for (const auto& [id, connection] : connections)
                {
                    if (connection.phase != Phase::Authenticated && now > connection.handshakeDeadline)
                        expired.push_back(TransportConnectionId{ id });
                }
                for (const auto id : expired)
                    failLocked(id, "secure handshake deadline exceeded");
            }
        }

        void fail(TransportConnectionId connection, std::string detail)
        {
            std::scoped_lock lock(mutex);
            failLocked(connection, std::move(detail));
        }

        void failLocked(TransportConnectionId connection, std::string detail)
        {
            connections.erase(connection.value);
            if (pendingOutboundConnection == connection)
                pendingOutboundConnection = {};
            transport->disconnect(connection);
            events.tryPush(
                { TransportEventType::Disconnected, connection, {}, std::move(detail) });
        }
    };

    SecureTransport::SecureTransport(
        std::unique_ptr<ITransport> transport, security::ServerIdentity identity)
        : mImpl(std::make_unique<Impl>(std::move(transport), std::move(identity)))
    {
    }

    SecureTransport::SecureTransport(
        std::unique_ptr<ITransport> transport, security::TrustStore trustStore)
        : mImpl(std::make_unique<Impl>(std::move(transport), std::move(trustStore)))
    {
    }

    SecureTransport::~SecureTransport() = default;

    bool SecureTransport::listen(const ListenOptions& options, TransportError& error)
    {
        error = {};
        return mImpl->listen(options, error);
    }

    bool SecureTransport::connect(
        const ConnectOptions& options, TransportConnectionId& connection, TransportError& error)
    {
        error = {};
        return mImpl->connect(options, connection, error);
    }

    bool SecureTransport::send(TransportMessage message, TransportError& error)
    {
        error = {};
        return mImpl->send(std::move(message), error);
    }

    std::optional<TransportEvent> SecureTransport::poll(std::chrono::milliseconds timeout)
    {
        return mImpl->poll(timeout);
    }

    std::optional<std::string> SecureTransport::peerAddress(
        TransportConnectionId connection) const
    {
        return mImpl->transport->peerAddress(connection);
    }

    void SecureTransport::disconnect(TransportConnectionId connection)
    {
        {
            std::scoped_lock lock(mImpl->mutex);
            mImpl->connections.erase(connection.value);
            if (mImpl->pendingOutboundConnection == connection)
                mImpl->pendingOutboundConnection = {};
        }
        mImpl->transport->disconnect(connection);
    }

    void SecureTransport::shutdown(std::chrono::milliseconds timeout)
    {
        {
            std::scoped_lock lock(mImpl->mutex);
            mImpl->connections.clear();
            mImpl->pendingOutboundConnection = {};
            mImpl->connectInProgress = false;
        }
        mImpl->transport->shutdown(timeout);
        mImpl->events.close();
    }

    bool SecureTransport::confirmFingerprint(TransportConnectionId connection,
        std::string_view fingerprint, TransportError& error)
    {
        error = {};
        return mImpl->confirmFingerprint(connection, fingerprint, error);
    }

    std::optional<std::string> SecureTransport::serverFingerprint() const
    {
        if (!mImpl->identity)
            return std::nullopt;
        return mImpl->identity->fingerprint();
    }
}
