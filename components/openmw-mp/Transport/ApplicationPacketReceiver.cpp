#include "ApplicationPacketReceiver.hpp"

#include <utility>

namespace mwmp::transport
{
    ApplicationPacketReceiver::ApplicationPacketReceiver(ApplicationPacketFlow flow,
        std::size_t maximumSnapshotSubjects)
        : mFlow(flow)
        , mSequences(maximumSnapshotSubjects)
    {
    }

    ApplicationReceiveResult ApplicationPacketReceiver::receive(
        const TransportMessage& message, ReceivedApplicationPacket& packet)
    {
        ApplicationPacket decoded;
        const protocol::DecodeResult decode = decodeApplicationPacket(message, mFlow, decoded);
        if (!decode)
            return { ApplicationReceiveStatus::Invalid, decode };

        if (message.delivery == DeliveryMode::Unreliable
            && !mSequences.accept(message.connection, message.lane,
                decoded.subject, decoded.sequence))
        {
            return { ApplicationReceiveStatus::StaleSnapshot, decode };
        }

        ReceivedApplicationPacket accepted;
        accepted.sender = message.connection;
        accepted.id = decoded.id;
        accepted.subject = decoded.subject;
        accepted.sequence = decoded.sequence;
        accepted.payload = std::move(decoded.payload);
        packet = std::move(accepted);
        return { ApplicationReceiveStatus::Accepted, decode };
    }

    void ApplicationPacketReceiver::removeConnection(TransportConnectionId connection)
    {
        mSequences.erase(connection);
    }

    void ApplicationPacketReceiver::clear()
    {
        mSequences.clear();
    }
}
