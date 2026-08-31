#include "PacketPlayerPosition.hpp"
#include <components/openmw-mp/NetworkMessages.hpp>

using namespace mwmp;

PacketPlayerPosition::PacketPlayerPosition() : PlayerPacket()
{
    packetID = ID_PLAYER_POSITION;
}

void PacketPlayerPosition::Packet(RakNet::BitStream *newBitstream, bool send)
{
    PlayerPacket::Packet(newBitstream, send);

    RW(player->position, send, 1);
    RW(player->direction, send, 1);
}
