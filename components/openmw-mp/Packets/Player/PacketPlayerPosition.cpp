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

    Field(player->position, 1);
    Field(player->direction, 1);
}
