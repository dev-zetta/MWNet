#include <components/openmw-mp/NetworkMessages.hpp>
#include <PacketPriority.h>
#include "WorldstatePacket.hpp"

using namespace mwmp;

WorldstatePacket::WorldstatePacket() : BasePacket()
{
    packetID = 0;
    priority = HIGH_PRIORITY;
    reliability = RELIABLE_ORDERED;
    orderChannel = CHANNEL_WORLDSTATE;
}

WorldstatePacket::~WorldstatePacket()
{

}

void WorldstatePacket::setWorldstate(BaseWorldstate *newWorldstate)
{
    worldstate = newWorldstate;
    guid = worldstate->guid;
}

BaseWorldstate *WorldstatePacket::getWorldstate()
{
    return worldstate;
}
