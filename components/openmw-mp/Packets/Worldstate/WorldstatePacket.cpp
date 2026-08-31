#include <components/openmw-mp/NetworkMessages.hpp>
#include "WorldstatePacket.hpp"

using namespace mwmp;

WorldstatePacket::WorldstatePacket() : BasePacket()
{
    packetID = 0;
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
