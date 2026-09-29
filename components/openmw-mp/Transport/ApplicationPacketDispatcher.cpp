#include "ApplicationPacketDispatcher.hpp"

#include <algorithm>
#include <chrono>
#include <vector>

namespace mwmp::transport
{
    ApplicationPacketDispatcher::ApplicationPacketDispatcher(ITransport& transport,
        ApplicationPacketFlow flow, std::size_t maximumConnections,
        metrics::ServerMetrics* metrics)
        : mTransport(transport)
        , mMetrics(metrics)
        , mFlow(flow)
        , mMaximumConnections(maximumConnections)
    {
    }

    bool ApplicationPacketDispatcher::addConnection(TransportConnectionId connection)
    {
        if (!connection)
            return false;
        std::scoped_lock lock(mMutex);
        if (mConnections.contains(connection.value))
            return true;
        if (mConnections.size() >= mMaximumConnections)
            return false;
        return mConnections.insert(connection.value).second;
    }

    void ApplicationPacketDispatcher::removeConnection(TransportConnectionId connection)
    {
        std::scoped_lock lock(mMutex);
        mConnections.erase(connection.value);
    }

    bool ApplicationPacketDispatcher::contains(TransportConnectionId connection) const
    {
        std::scoped_lock lock(mMutex);
        return mConnections.contains(connection.value);
    }

    std::size_t ApplicationPacketDispatcher::connectionCount() const
    {
        std::scoped_lock lock(mMutex);
        return mConnections.size();
    }

    bool ApplicationPacketDispatcher::sendToServer(protocol::ApplicationPacketId id,
        std::uint64_t subject, std::span<const std::byte> payload, TransportError& error)
    {
        if (mFlow != ApplicationPacketFlow::ClientToServer)
        {
            error = { TransportErrorCode::InvalidConfiguration,
                "only a client dispatcher can send to its server" };
            return false;
        }

        TransportConnectionId server;
        {
            std::scoped_lock lock(mMutex);
            if (mConnections.size() != 1)
            {
                error = { TransportErrorCode::Closed,
                    "client dispatcher does not have exactly one server connection" };
                return false;
            }
            server.value = *mConnections.begin();
        }
        return sendOne(id, subject, server, payload, error);
    }

    bool ApplicationPacketDispatcher::sendTo(protocol::ApplicationPacketId id,
        std::uint64_t subject, TransportConnectionId destination,
        std::span<const std::byte> payload, TransportError& error)
    {
        if (mFlow != ApplicationPacketFlow::ServerToClient)
        {
            error = { TransportErrorCode::InvalidConfiguration,
                "only a server dispatcher can address a client connection" };
            return false;
        }
        if (!contains(destination))
        {
            error = { TransportErrorCode::Closed,
                "application packet destination is not connected" };
            return false;
        }
        return sendOne(id, subject, destination, payload, error);
    }

    bool ApplicationPacketDispatcher::sendToAll(protocol::ApplicationPacketId id,
        std::uint64_t subject, std::span<const std::byte> payload, TransportError& error,
        std::optional<TransportConnectionId> excluded)
    {
        if (mFlow != ApplicationPacketFlow::ServerToClient)
        {
            error = { TransportErrorCode::InvalidConfiguration,
                "only a server dispatcher can broadcast application packets" };
            return false;
        }

        std::vector<std::uint64_t> destinations;
        {
            std::scoped_lock lock(mMutex);
            destinations.reserve(mConnections.size());
            for (const std::uint64_t connection : mConnections)
            {
                if (!excluded || connection != excluded->value)
                    destinations.push_back(connection);
            }
        }
        std::ranges::sort(destinations);

        for (const std::uint64_t destination : destinations)
        {
            if (!sendOne(id, subject, TransportConnectionId{ destination }, payload, error))
                return false;
        }
        error = {};
        return true;
    }

    bool ApplicationPacketDispatcher::sendOne(protocol::ApplicationPacketId id,
        std::uint64_t subject, TransportConnectionId destination,
        std::span<const std::byte> payload, TransportError& error)
    {
        ApplicationPacketRoute route;
        if (!applicationPacketRoute(id, mFlow, route))
        {
            error = { TransportErrorCode::MessageRejected,
                "application packet has no MWNet route" };
            return false;
        }

        TransportMessage message;
        protocol::CodecError codecError = protocol::CodecError::None;
        const auto started = std::chrono::steady_clock::now();
        if (!encodeApplicationPacket(id, mFlow, destination, subject,
                nextSequence(route.lane), payload, message, codecError))
        {
            if (mMetrics != nullptr)
                mMetrics->observeSerialization(
                    std::chrono::steady_clock::now() - started);
            error = { TransportErrorCode::MessageRejected,
                std::string("application packet encoding failed: ")
                    + protocol::describe(codecError) };
            return false;
        }
        if (mMetrics != nullptr)
            mMetrics->observeSerialization(
                std::chrono::steady_clock::now() - started);
        const std::size_t messageBytes
            = protocol::envelopeBytes + message.payload.size();
        const bool sent = mTransport.send(std::move(message), error);
        if (sent && mMetrics != nullptr)
            mMetrics->recordOutbound(destination.value, messageBytes);
        return sent;
    }

    std::uint64_t ApplicationPacketDispatcher::nextSequence(MessageLane lane) noexcept
    {
        auto& sequence = mSequences.at(static_cast<std::size_t>(lane));
        std::uint64_t value = sequence.fetch_add(1, std::memory_order_relaxed) + 1;
        if (value == 0)
            value = sequence.fetch_add(1, std::memory_order_relaxed) + 1;
        return value;
    }
}
