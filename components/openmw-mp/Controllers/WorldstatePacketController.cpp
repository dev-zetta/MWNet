#include "../Packets/Worldstate/PacketCellReset.hpp"
#include "../Packets/Worldstate/PacketClientScriptGlobal.hpp"
#include "../Packets/Worldstate/PacketClientScriptSettings.hpp"
#include "../Packets/Worldstate/PacketRecordDynamic.hpp"
#include "../Packets/Worldstate/PacketWorldCollisionOverride.hpp"
#include "../Packets/Worldstate/PacketWorldDestinationOverride.hpp"
#include "../Packets/Worldstate/PacketWorldKillCount.hpp"
#include "../Packets/Worldstate/PacketWorldMap.hpp"
#include "../Packets/Worldstate/PacketWorldRegionAuthority.hpp"
#include "../Packets/Worldstate/PacketWorldTime.hpp"
#include "../Packets/Worldstate/PacketWorldWeather.hpp"

#include "WorldstatePacketController.hpp"

template <typename T>
inline void AddPacket(mwmp::WorldstatePacketController::packets_t *packets)
{
    auto packet = std::make_unique<T>();
    const auto id = packet->GetPacketID();
    packets->emplace(id, std::move(packet));
}

mwmp::WorldstatePacketController::WorldstatePacketController()
{
    AddPacket<PacketCellReset>(&packets);
    AddPacket<PacketClientScriptGlobal>(&packets);
    AddPacket<PacketClientScriptSettings>(&packets);
    AddPacket<PacketRecordDynamic>(&packets);
    AddPacket<PacketWorldCollisionOverride>(&packets);
    AddPacket<PacketWorldDestinationOverride>(&packets);
    AddPacket<PacketWorldKillCount>(&packets);
    AddPacket<PacketWorldMap>(&packets);
    AddPacket<PacketWorldRegionAuthority>(&packets);
    AddPacket<PacketWorldTime>(&packets);
    AddPacket<PacketWorldWeather>(&packets);
}


mwmp::WorldstatePacket *mwmp::WorldstatePacketController::GetPacket(std::uint16_t id)
{
    const auto packet = packets.find(static_cast<unsigned char>(id));
    return packet == packets.end() ? nullptr : packet->second.get();
}

void mwmp::WorldstatePacketController::SetStream(RakNet::BitStream *inStream, RakNet::BitStream *outStream)
{
    for(const auto &packet : packets)
        packet.second->SetStreams(inStream, outStream);
}

void mwmp::WorldstatePacketController::SetApplicationPacketDispatcher(
    transport::ApplicationPacketDispatcher* dispatcher)
{
    for (const auto& packet : packets)
        packet.second->SetApplicationPacketDispatcher(dispatcher);
}

bool mwmp::WorldstatePacketController::ContainsPacket(std::uint16_t id)
{
    return packets.find(static_cast<unsigned char>(id)) != packets.end();
}
