#ifndef OPENMW_MP_TRANSPORT_ITRANSPORT_HPP
#define OPENMW_MP_TRANSPORT_ITRANSPORT_HPP

#include <chrono>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace mwmp::transport
{
    struct TransportConnectionId
    {
        static constexpr std::size_t wireSize = sizeof(std::uint64_t);

        std::uint64_t value = 0;

        constexpr TransportConnectionId() noexcept = default;
        constexpr TransportConnectionId(std::uint64_t id) noexcept
            : value(id)
        {
        }

        explicit operator bool() const noexcept { return value != 0; }
        auto operator<=>(const TransportConnectionId&) const = default;
    };

    enum class DeliveryMode : std::uint8_t
    {
        ReliableOrdered,
        ReliableUnordered,
        Unreliable,
    };

    enum class MessageLane : std::uint8_t
    {
        System,
        Player,
        Actor,
        Object,
        Worldstate,
    };

    struct TransportMessage
    {
        TransportConnectionId connection;
        DeliveryMode delivery = DeliveryMode::ReliableOrdered;
        MessageLane lane = MessageLane::System;
        std::uint16_t messageType = 0;
        std::uint16_t flags = 0;
        std::uint64_t subject = 0;
        std::uint64_t sequence = 0;
        std::vector<std::byte> payload;
    };

    enum class TransportEventType : std::uint8_t
    {
        Connected,
        Disconnected,
        Message,
        TrustRequired,
    };

    struct TransportEvent
    {
        TransportEventType type = TransportEventType::Disconnected;
        TransportConnectionId connection;
        TransportMessage message;
        std::string detail;
    };

    enum class TransportErrorCode : std::uint8_t
    {
        None,
        InvalidConfiguration,
        ConnectionFailed,
        HandshakeFailed,
        Timeout,
        QueueFull,
        MessageRejected,
        SecurityFailure,
        Closed,
        Internal,
    };

    struct TransportError
    {
        TransportErrorCode code = TransportErrorCode::None;
        std::string detail;

        explicit operator bool() const noexcept { return code != TransportErrorCode::None; }
    };

    struct TransportTimeouts
    {
        std::chrono::milliseconds connect{ 10'000 };
        std::chrono::milliseconds handshake{ 10'000 };
        std::chrono::milliseconds read{ 30'000 };
        std::chrono::milliseconds shutdown{ 5'000 };
    };

    struct ListenOptions
    {
        std::string address = "127.0.0.1";
        std::uint16_t port = 25565;
        std::size_t maximumConnections = 64;
        bool publicListen = false;
        TransportTimeouts timeouts;
    };

    struct ConnectOptions
    {
        std::string host;
        std::uint16_t port = 25565;
        std::optional<std::string> trustedFingerprint;
        // Enforce an identity without granting trust or changing saved pins.
        std::optional<std::string> expectedFingerprint;
        TransportTimeouts timeouts;
    };

    class ITransport
    {
    public:
        virtual ~ITransport() = default;

        virtual bool listen(const ListenOptions& options, TransportError& error) = 0;
        virtual bool connect(
            const ConnectOptions& options, TransportConnectionId& connection, TransportError& error)
            = 0;
        virtual bool send(TransportMessage message, TransportError& error) = 0;
        virtual std::optional<TransportEvent> poll(std::chrono::milliseconds timeout) = 0;
        virtual std::optional<std::string> peerAddress(TransportConnectionId connection) const
        {
            (void)connection;
            return std::nullopt;
        }
        virtual void disconnect(TransportConnectionId connection) = 0;
        virtual void shutdown(std::chrono::milliseconds timeout) = 0;
    };
}

#endif
