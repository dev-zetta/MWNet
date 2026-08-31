#include "PacketWorldKillCount.hpp"
#include <components/openmw-mp/NetworkMessages.hpp>

using namespace mwmp;

PacketWorldKillCount::PacketWorldKillCount() : WorldstatePacket()
{
    packetID = ID_WORLD_KILL_COUNT;
    orderChannel = CHANNEL_SYSTEM;
}

void PacketWorldKillCount::Packet(RakNet::BitStream *newBitstream, bool send)
{
    WorldstatePacket::Packet(newBitstream, send);

    uint32_t killChangesCount = 0;

    if (send)
        killChangesCount = static_cast<uint32_t>(worldstate->killChanges.size());

    if (!RWCount(killChangesCount, send))
        return;

    if (!send)
    {
        worldstate->killChanges.clear();
        worldstate->killChanges.resize(killChangesCount);
    }

    for (auto &&killChange : worldstate->killChanges)
    {
        RW(killChange.refId, send, true);
        RW(killChange.number, send);
    }
}
