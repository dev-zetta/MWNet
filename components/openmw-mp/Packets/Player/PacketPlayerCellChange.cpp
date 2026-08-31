#include <components/openmw-mp/NetworkMessages.hpp>
#include "PacketPlayerCellChange.hpp"


mwmp::PacketPlayerCellChange::PacketPlayerCellChange() : PlayerPacket()
{
    packetID = ID_PLAYER_CELL_CHANGE;
}

void mwmp::PacketPlayerCellChange::Packet(RakNet::BitStream *newBitstream, bool send)
{
    PlayerPacket::Packet(newBitstream, send);

    RW(player->cell.mData, send, true);
    RW(player->cell.mName, send, true);

    RW(player->previousCellPosition.pos, send, true);

    RW(player->isChangingRegion, send);

    if (player->isChangingRegion)
        RW(player->cell.mRegion, send, true);
}
