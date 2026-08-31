#include "../Packets/Object/PacketObjectActivate.hpp"
#include "../Packets/Object/PacketObjectAnimPlay.hpp"
#include "../Packets/Object/PacketObjectAttach.hpp"
#include "../Packets/Object/PacketObjectDelete.hpp"
#include "../Packets/Object/PacketObjectDialogueChoice.hpp"
#include "../Packets/Object/PacketObjectHit.hpp"
#include "../Packets/Object/PacketObjectLock.hpp"
#include "../Packets/Object/PacketObjectMiscellaneous.hpp"
#include "../Packets/Object/PacketObjectMove.hpp"
#include "../Packets/Object/PacketObjectPlace.hpp"
#include "../Packets/Object/PacketObjectRestock.hpp"
#include "../Packets/Object/PacketObjectRotate.hpp"
#include "../Packets/Object/PacketObjectScale.hpp"
#include "../Packets/Object/PacketObjectSound.hpp"
#include "../Packets/Object/PacketObjectSpawn.hpp"
#include "../Packets/Object/PacketObjectState.hpp"
#include "../Packets/Object/PacketObjectTrap.hpp"

#include "../Packets/Object/PacketContainer.hpp"
#include "../Packets/Object/PacketDoorDestination.hpp"
#include "../Packets/Object/PacketDoorState.hpp"
#include "../Packets/Object/PacketMusicPlay.hpp"
#include "../Packets/Object/PacketVideoPlay.hpp"

#include "../Packets/Object/PacketConsoleCommand.hpp"
#include "../Packets/Object/PacketClientScriptLocal.hpp"
#include "../Packets/Object/PacketScriptMemberShort.hpp"

#include "ObjectPacketController.hpp"

template <typename T>
inline void AddPacket(mwmp::ObjectPacketController::packets_t *packets)
{
    auto packet = std::make_unique<T>();
    const auto id = packet->GetPacketID();
    packets->emplace(id, std::move(packet));
}

mwmp::ObjectPacketController::ObjectPacketController()
{
    AddPacket<PacketObjectActivate>(&packets);
    AddPacket<PacketObjectAnimPlay>(&packets);
    AddPacket<PacketObjectAttach>(&packets);
    AddPacket<PacketObjectDelete>(&packets);
    AddPacket<PacketObjectDialogueChoice>(&packets);
    AddPacket<PacketObjectHit>(&packets);
    AddPacket<PacketObjectLock>(&packets);
    AddPacket<PacketObjectMiscellaneous>(&packets);
    AddPacket<PacketObjectMove>(&packets);
    AddPacket<PacketObjectPlace>(&packets);
    AddPacket<PacketObjectRestock>(&packets);
    AddPacket<PacketObjectRotate>(&packets);
    AddPacket<PacketObjectScale>(&packets);
    AddPacket<PacketObjectSound>(&packets);
    AddPacket<PacketObjectSpawn>(&packets);
    AddPacket<PacketObjectState>(&packets);
    AddPacket<PacketObjectTrap>(&packets);
    
    AddPacket<PacketContainer>(&packets);
    AddPacket<PacketDoorDestination>(&packets);
    AddPacket<PacketDoorState>(&packets);
    AddPacket<PacketMusicPlay>(&packets);
    AddPacket<PacketVideoPlay>(&packets);

    AddPacket<PacketConsoleCommand>(&packets);
    AddPacket<PacketClientScriptLocal>(&packets);
    AddPacket<PacketScriptMemberShort>(&packets);
}


mwmp::ObjectPacket *mwmp::ObjectPacketController::GetPacket(RakNet::MessageID id)
{
    const auto packet = packets.find(static_cast<unsigned char>(id));
    return packet == packets.end() ? nullptr : packet->second.get();
}

void mwmp::ObjectPacketController::SetStream(RakNet::BitStream *inStream, RakNet::BitStream *outStream)
{
    for(const auto &packet : packets)
        packet.second->SetStreams(inStream, outStream);
}

void mwmp::ObjectPacketController::SetApplicationPacketDispatcher(
    transport::ApplicationPacketDispatcher* dispatcher)
{
    for (const auto& packet : packets)
        packet.second->SetApplicationPacketDispatcher(dispatcher);
}

bool mwmp::ObjectPacketController::ContainsPacket(RakNet::MessageID id)
{
    return packets.find(static_cast<unsigned char>(id)) != packets.end();
}
