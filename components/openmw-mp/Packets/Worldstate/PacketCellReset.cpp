#include "PacketCellReset.hpp"
#include <components/openmw-mp/NetworkMessages.hpp>

using namespace mwmp;

PacketCellReset::PacketCellReset() : WorldstatePacket()
{
    packetID = ID_CELL_RESET;
}

void PacketCellReset::Packet(bool send)
{
    WorldstatePacket::Packet(send);

    uint32_t cellCount = 0;

    if (send)
        cellCount = static_cast<uint32_t>(worldstate->cellsToReset.size());

    if (!RWCount(cellCount, send))
        return;

    if (!send)
    {
        worldstate->cellsToReset.clear();
        worldstate->cellsToReset.resize(cellCount);
    }

    for (auto &&cellToReset : worldstate->cellsToReset)
    {
        RW(cellToReset.mData, send, true);
        RW(cellToReset.mName, send, true);
    }
}
