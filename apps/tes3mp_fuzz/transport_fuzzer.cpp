#include <components/openmw-mp/Transport/ApplicationPacketBridge.hpp>
#include <components/openmw-mp/Transport/ApplicationPacketReceiver.hpp>
#include <components/openmw-mp/Transport/TransportCodec.hpp>

#include <cstddef>
#include <cstdint>
#include <span>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size)
{
    const auto bytes = std::as_bytes(std::span(data, size));

    mwmp::transport::TransportMessage decoded;
    (void)mwmp::transport::decodeTransportMessage(bytes,
        mwmp::transport::TransportConnectionId{ 1 },
        mwmp::transport::MessageLane::System,
        mwmp::transport::DeliveryMode::ReliableOrdered, decoded);

    mwmp::transport::TransportMessage application;
    application.connection = mwmp::transport::TransportConnectionId{ 1 };
    application.messageType = size > 0 ? data[0] : 0;
    application.subject = size > 1 ? data[1] : 0;
    application.sequence = size > 2 ? data[2] : 0;
    application.payload.assign(bytes.begin(), bytes.end());

    mwmp::transport::ApplicationPacket packet;
    (void)mwmp::transport::decodeApplicationPacket(application,
        mwmp::transport::ApplicationPacketFlow::ClientToServer, packet);
    (void)mwmp::transport::decodeApplicationPacket(application,
        mwmp::transport::ApplicationPacketFlow::ServerToClient, packet);

    mwmp::transport::ReceivedApplicationPacket received;
    mwmp::transport::ApplicationPacketReceiver server(
        mwmp::transport::ApplicationPacketFlow::ClientToServer);
    mwmp::transport::ApplicationPacketReceiver client(
        mwmp::transport::ApplicationPacketFlow::ServerToClient);
    (void)server.receive(application, received);
    (void)client.receive(application, received);
    return 0;
}
