#include "../Packets/System/PacketSystemHandshake.hpp"

#include "SystemPacketController.hpp"

template <typename T>
inline void AddPacket(mwmp::SystemPacketController::packets_t *packets)
{
    auto packet = std::make_unique<T>();
    const auto id = packet->GetPacketID();
    packets->emplace(id, std::move(packet));
}

mwmp::SystemPacketController::SystemPacketController()
{
    AddPacket<PacketSystemHandshake>(&packets);
}


mwmp::SystemPacket *mwmp::SystemPacketController::GetPacket(RakNet::MessageID id)
{
    const auto packet = packets.find(static_cast<unsigned char>(id));
    return packet == packets.end() ? nullptr : packet->second.get();
}

void mwmp::SystemPacketController::SetStream(RakNet::BitStream *inStream, RakNet::BitStream *outStream)
{
    for(const auto &packet : packets)
        packet.second->SetStreams(inStream, outStream);
}

void mwmp::SystemPacketController::SetApplicationPacketDispatcher(
    transport::ApplicationPacketDispatcher* dispatcher)
{
    for (const auto& packet : packets)
        packet.second->SetApplicationPacketDispatcher(dispatcher);
}

bool mwmp::SystemPacketController::ContainsPacket(RakNet::MessageID id)
{
    return packets.find(static_cast<unsigned char>(id)) != packets.end();
}
