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

    // Bind unreliable snapshots to a cell so they cannot be applied across
    // a reliable cell transition that overtakes (or is overtaken by) them.
    Field(player->cell.mData, true);
    Field(player->cell.mName, true);
    Field(player->position, 1);
    Field(player->direction, 1);
}
