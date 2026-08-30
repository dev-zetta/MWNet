#ifndef OPENMW_MP_TRANSPORT_APPLICATION_PACKET_RECEIVER_HPP
#define OPENMW_MP_TRANSPORT_APPLICATION_PACKET_RECEIVER_HPP

#include "ApplicationPacketBridge.hpp"
#include "SnapshotSequenceTracker.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace mwmp::transport
{
    enum class ApplicationReceiveStatus : std::uint8_t
    {
        Accepted,
        Invalid,
        StaleSnapshot,
    };

    struct ReceivedApplicationPacket
    {
        TransportConnectionId sender;
        protocol::ApplicationPacketId id = protocol::ApplicationPacketId::UserMyId;
        std::uint64_t subject = 0;
        std::uint64_t sequence = 0;
        std::vector<std::byte> payload;
    };

    struct ApplicationReceiveResult
    {
        ApplicationReceiveStatus status = ApplicationReceiveStatus::Invalid;
        protocol::DecodeResult decode;

        explicit operator bool() const noexcept
        {
            return status == ApplicationReceiveStatus::Accepted
                && static_cast<bool>(decode);
        }
    };

    class ApplicationPacketReceiver
    {
    public:
        explicit ApplicationPacketReceiver(ApplicationPacketFlow flow,
            std::size_t maximumSnapshotSubjects = 4096);

        ApplicationReceiveResult receive(
            const TransportMessage& message, ReceivedApplicationPacket& packet);
        void removeConnection(TransportConnectionId connection);
        void clear();

    private:
        ApplicationPacketFlow mFlow;
        SnapshotSequenceTracker mSequences;
    };
}

#endif
