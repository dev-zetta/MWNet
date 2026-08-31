#include <components/openmw-mp/NetworkMessages.hpp>
#include "PacketPlayerCellState.hpp"


mwmp::PacketPlayerCellState::PacketPlayerCellState() : PlayerPacket()
{
    packetID = ID_PLAYER_CELL_STATE;
}

void mwmp::PacketPlayerCellState::Packet(RakNet::BitStream *newBitstream, bool send)
{
    PlayerPacket::Packet(newBitstream, send);

    uint32_t count = 0;

    if (send)
        count = static_cast<uint32_t>(player->cellStateChanges.size());

    if (!RWCount(count, send))
        return;

    if (!send)
    {
        player->cellStateChanges.clear();
        player->cellStateChanges.resize(count);
    }

    for (auto &&cellState : player->cellStateChanges)
    {
        RW(cellState.type, send);
        RW(cellState.cell.mData, send, true);
        RW(cellState.cell.mName, send, true);
    }
}
