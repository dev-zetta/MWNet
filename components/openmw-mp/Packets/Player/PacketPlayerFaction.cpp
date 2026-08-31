#include <components/openmw-mp/NetworkMessages.hpp>
#include "PacketPlayerFaction.hpp"

using namespace mwmp;

PacketPlayerFaction::PacketPlayerFaction() : PlayerPacket()
{
    packetID = ID_PLAYER_FACTION;
}

void PacketPlayerFaction::Packet(bool send)
{
    PlayerPacket::Packet(send);

    Field(player->factionChanges.action);

    uint32_t count = 0;

    if (send)
        count = static_cast<uint32_t>(player->factionChanges.factions.size());

    if (!CollectionSize(count))
        return;

    if (!send)
    {
        player->factionChanges.factions.clear();
        player->factionChanges.factions.resize(count);
    }

    for (auto &&faction : player->factionChanges.factions)
    {
        Field(faction.factionId, true);

        if (player->factionChanges.action == FactionChanges::RANK)
            Field(faction.rank);

        if (player->factionChanges.action == FactionChanges::EXPULSION)
            Field(faction.isExpelled);

        if (player->factionChanges.action == FactionChanges::REPUTATION)
            Field(faction.reputation);
    }
}
