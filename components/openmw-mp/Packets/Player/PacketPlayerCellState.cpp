#include <components/openmw-mp/NetworkMessages.hpp>
#include "PacketPlayerCellState.hpp"


mwmp::PacketPlayerCellState::PacketPlayerCellState() : PlayerPacket()
{
    packetID = ID_PLAYER_CELL_STATE;
}

void mwmp::PacketPlayerCellState::Packet(bool send)
{
    PlayerPacket::Packet(send);

    uint32_t count = 0;

    if (send)
        count = static_cast<uint32_t>(player->cellStateChanges.size());

    if (!CollectionSize(count))
        return;

    if (!send)
    {
        player->cellStateChanges.clear();
        player->cellStateChanges.resize(count);
    }

    for (auto &&cellState : player->cellStateChanges)
    {
        Field(cellState.type);
        Field(cellState.cell.mData, true);
        Field(cellState.cell.mName, true);
    }
}
