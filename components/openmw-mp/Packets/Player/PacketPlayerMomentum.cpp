#include "PacketPlayerMomentum.hpp"
#include <components/openmw-mp/NetworkMessages.hpp>

using namespace mwmp;

PacketPlayerMomentum::PacketPlayerMomentum() : PlayerPacket()
{
    packetID = ID_PLAYER_MOMENTUM;
}

void PacketPlayerMomentum::Packet(bool send)
{
    PlayerPacket::Packet(send);
    
    RW(player->momentum.pos, send, true);
}
