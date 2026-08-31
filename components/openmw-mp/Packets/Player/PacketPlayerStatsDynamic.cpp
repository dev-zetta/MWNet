#include "PacketPlayerStatsDynamic.hpp"

#include <components/openmw-mp/NetworkMessages.hpp>

using namespace mwmp;

PacketPlayerStatsDynamic::PacketPlayerStatsDynamic() : PlayerPacket()
{
    packetID = ID_PLAYER_STATS_DYNAMIC;
}

void PacketPlayerStatsDynamic::Packet(bool send)
{
    PlayerPacket::Packet(send);

    Field(player->exchangeFullInfo);

    if (player->exchangeFullInfo)
    {
        Field(player->creatureStats.mDynamic);
    }
    else
    {
        uint32_t count = 0;

        if (send)
            count = static_cast<uint32_t>(player->statsDynamicIndexChanges.size());

        if (!CollectionSize(count, 3))
            return;

        if (!send)
        {
            player->statsDynamicIndexChanges.clear();
            player->statsDynamicIndexChanges.resize(count);
        }

        for (auto &&statsDynamicIndex : player->statsDynamicIndexChanges)
        {
            Field(statsDynamicIndex);
            if (statsDynamicIndex >= 3)
            {
                packetValid = false;
                return;
            }
            Field(player->creatureStats.mDynamic[statsDynamicIndex]);
        }
    }
}
