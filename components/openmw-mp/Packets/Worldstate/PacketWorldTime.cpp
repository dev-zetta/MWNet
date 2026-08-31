#include <components/openmw-mp/NetworkMessages.hpp>
#include "PacketWorldTime.hpp"

using namespace mwmp;

PacketWorldTime::PacketWorldTime() : WorldstatePacket()
{
    packetID = ID_WORLD_TIME;
}

void PacketWorldTime::Packet(bool send)
{
    WorldstatePacket::Packet(send);

    Field(worldstate->time.hour);
    Field(worldstate->time.day);
    Field(worldstate->time.month);
    Field(worldstate->time.year);

    Field(worldstate->time.daysPassed);
    Field(worldstate->time.timeScale);
}
