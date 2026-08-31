#ifndef OPENMW_MP_TRANSPORT_APPLICATION_PACKET_DISPATCHER_HPP
#define OPENMW_MP_TRANSPORT_APPLICATION_PACKET_DISPATCHER_HPP

#include "ApplicationPacketBridge.hpp"

#include <components/openmw-mp/Metrics/ServerMetrics.hpp>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <span>
#include <unordered_set>

namespace mwmp::transport
{
    class ApplicationPacketDispatcher
    {
    public:
        ApplicationPacketDispatcher(ITransport& transport, ApplicationPacketFlow flow,
            std::size_t maximumConnections = 64,
            metrics::ServerMetrics* metrics = nullptr);

        bool addConnection(TransportConnectionId connection);
        void removeConnection(TransportConnectionId connection);
        bool contains(TransportConnectionId connection) const;
        std::size_t connectionCount() const;

        bool sendToServer(protocol::ApplicationPacketId id, std::uint64_t subject,
            std::span<const std::byte> payload, TransportError& error);
        bool sendTo(protocol::ApplicationPacketId id, std::uint64_t subject,
            TransportConnectionId destination, std::span<const std::byte> payload,
            TransportError& error);
        bool sendToAll(protocol::ApplicationPacketId id, std::uint64_t subject,
            std::span<const std::byte> payload, TransportError& error,
            std::optional<TransportConnectionId> excluded = std::nullopt);

        ApplicationPacketFlow flow() const noexcept { return mFlow; }

    private:
        bool sendOne(protocol::ApplicationPacketId id, std::uint64_t subject,
            TransportConnectionId destination, std::span<const std::byte> payload,
            TransportError& error);
        std::uint64_t nextSequence(MessageLane lane) noexcept;

        ITransport& mTransport;
        metrics::ServerMetrics* mMetrics;
        ApplicationPacketFlow mFlow;
        std::size_t mMaximumConnections;
        mutable std::mutex mMutex;
        std::unordered_set<std::uint64_t> mConnections;
        std::array<std::atomic<std::uint64_t>, 5> mSequences{};
    };
}

#endif
