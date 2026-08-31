#include <components/openmw-mp/NetworkMessages.hpp>
#include "PacketPlayerAlly.hpp"

mwmp::PacketPlayerAlly::PacketPlayerAlly() : PlayerPacket()
{
    packetID = ID_PLAYER_ALLY;
}

void mwmp::PacketPlayerAlly::Packet(bool send)
{
    PlayerPacket::Packet(send);

    uint32_t count = 0;

    if (send)
        count = static_cast<uint32_t>(player->alliedPlayers.size());

    if (!CollectionSize(count))
        return;

    if (!send)
    {
        player->alliedPlayers.clear();
        player->alliedPlayers.resize(count);
    }

    for (auto &&teamPlayerGuid : player->alliedPlayers)
    {
        Field(teamPlayerGuid, true);
    }
}
