#include "../Packets/Actor/PacketActorList.hpp"
#include "../Packets/Actor/PacketActorAuthority.hpp"
#include "../Packets/Actor/PacketActorTest.hpp"
#include "../Packets/Actor/PacketActorAI.hpp"
#include "../Packets/Actor/PacketActorAnimFlags.hpp"
#include "../Packets/Actor/PacketActorAnimPlay.hpp"
#include "../Packets/Actor/PacketActorAttack.hpp"
#include "../Packets/Actor/PacketActorCast.hpp"
#include "../Packets/Actor/PacketActorCellChange.hpp"
#include "../Packets/Actor/PacketActorDeath.hpp"
#include "../Packets/Actor/PacketActorEquipment.hpp"
#include "../Packets/Actor/PacketActorPosition.hpp"
#include "../Packets/Actor/PacketActorSpeech.hpp"
#include "../Packets/Actor/PacketActorSpellsActive.hpp"
#include "../Packets/Actor/PacketActorStatsDynamic.hpp"


#include "ActorPacketController.hpp"

template <typename T>
inline void AddPacket(mwmp::ActorPacketController::packets_t *packets)
{
    auto packet = std::make_unique<T>();
    const auto id = packet->GetPacketID();
    packets->emplace(id, std::move(packet));
}

mwmp::ActorPacketController::ActorPacketController()
{
    AddPacket<PacketActorList>(&packets);
    AddPacket<PacketActorAuthority>(&packets);
    AddPacket<PacketActorTest>(&packets);
    AddPacket<PacketActorAI>(&packets);
    AddPacket<PacketActorAnimFlags>(&packets);
    AddPacket<PacketActorAnimPlay>(&packets);
    AddPacket<PacketActorAttack>(&packets);
    AddPacket<PacketActorCast>(&packets);
    AddPacket<PacketActorCellChange>(&packets);
    AddPacket<PacketActorDeath>(&packets);
    AddPacket<PacketActorEquipment>(&packets);
    AddPacket<PacketActorPosition>(&packets);
    AddPacket<PacketActorSpeech>(&packets);
    AddPacket<PacketActorSpellsActive>(&packets);
    AddPacket<PacketActorStatsDynamic>(&packets);
}


mwmp::ActorPacket *mwmp::ActorPacketController::GetPacket(std::uint16_t id)
{
    const auto packet = packets.find(static_cast<unsigned char>(id));
    return packet == packets.end() ? nullptr : packet->second.get();
}

void mwmp::ActorPacketController::SetStream(RakNet::BitStream *inStream, RakNet::BitStream *outStream)
{
    for(const auto &packet : packets)
        packet.second->SetStreams(inStream, outStream);
}

void mwmp::ActorPacketController::SetApplicationPacketDispatcher(
    transport::ApplicationPacketDispatcher* dispatcher)
{
    for (const auto& packet : packets)
        packet.second->SetApplicationPacketDispatcher(dispatcher);
}

bool mwmp::ActorPacketController::ContainsPacket(std::uint16_t id)
{
    return packets.find(static_cast<unsigned char>(id)) != packets.end();
}
