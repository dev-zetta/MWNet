#include "PacketPlayerMomentum.hpp"
#include <components/openmw-mp/NetworkMessages.hpp>

using namespace mwmp;

PacketPlayerMomentum::PacketPlayerMomentum() : PlayerPacket()
{
    packetID = ID_PLAYER_MOMENTUM;
    priority = MEDIUM_PRIORITY;
}

void PacketPlayerMomentum::Packet(RakNet::BitStream *newBitstream, bool send)
{
    PlayerPacket::Packet(newBitstream, send);
    
    RW(player->momentum.pos, send, true);
}
