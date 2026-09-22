#include <components/openmw-mp/NetworkMessages.hpp>
#include "PacketPlayerCellChange.hpp"


mwmp::PacketPlayerCellChange::PacketPlayerCellChange() : PlayerPacket()
{
    packetID = ID_PLAYER_CELL_CHANGE;
}

void mwmp::PacketPlayerCellChange::Packet(bool send)
{
    PlayerPacket::Packet(send);

    Field(player->cell.mData, true);
    Field(player->cell.mName, true);

    Field(player->previousCellPosition.pos, true);
    Field(player->position, 1);

    Field(player->isChangingRegion);

    if (player->isChangingRegion)
        Field(player->cell.mRegion, true);
}
