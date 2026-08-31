#include "PacketPlayerResurrect.hpp"
#include <components/openmw-mp/NetworkMessages.hpp>
#include <components/openmw-mp/TimedLog.hpp>

using namespace mwmp;

PacketPlayerResurrect::PacketPlayerResurrect() : PlayerPacket()
{
    packetID = ID_PLAYER_RESURRECT;
}

void PacketPlayerResurrect::Packet(RakNet::BitStream *newBitstream, bool send)
{
    PlayerPacket::Packet(newBitstream, send);

    RW(player->resurrectType, send);
}
