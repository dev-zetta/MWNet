#ifndef OPENMW_MP_TRANSPORT_GAME_NETWORKING_SOCKETS_TRANSPORT_HPP
#define OPENMW_MP_TRANSPORT_GAME_NETWORKING_SOCKETS_TRANSPORT_HPP

#include "ITransport.hpp"

#include <cstdint>
#include <memory>
#include <optional>

namespace mwmp::transport
{
    class GameNetworkingSocketsTransport final : public ITransport
    {
    public:
        GameNetworkingSocketsTransport();
        ~GameNetworkingSocketsTransport() override;

        GameNetworkingSocketsTransport(const GameNetworkingSocketsTransport&) = delete;
        GameNetworkingSocketsTransport& operator=(const GameNetworkingSocketsTransport&) = delete;

        bool listen(const ListenOptions& options, TransportError& error) override;
        bool connect(const ConnectOptions& options, TransportConnectionId& connection,
            TransportError& error) override;
        bool send(TransportMessage message, TransportError& error) override;
        std::optional<TransportEvent> poll(std::chrono::milliseconds timeout) override;
        void disconnect(TransportConnectionId connection) override;
        void shutdown(std::chrono::milliseconds timeout) override;

        std::optional<std::uint16_t> boundPort() const;

    private:
        struct Impl;
        std::unique_ptr<Impl> mImpl;
    };
}

#endif
