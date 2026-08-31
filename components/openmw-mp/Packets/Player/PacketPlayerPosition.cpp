#include "PacketPlayerPosition.hpp"
#include <components/openmw-mp/NetworkMessages.hpp>

using namespace mwmp;

PacketPlayerPosition::PacketPlayerPosition() : PlayerPacket()
{
    packetID = ID_PLAYER_POSITION;
}

void PacketPlayerPosition::Packet(bool send)
{
    PlayerPacket::Packet(send);

    RW(player->position, send, 1);
    RW(player->direction, send, 1);
}
