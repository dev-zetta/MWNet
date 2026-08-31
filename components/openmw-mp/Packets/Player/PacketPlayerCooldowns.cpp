#include <components/openmw-mp/NetworkMessages.hpp>
#include "PacketPlayerCooldowns.hpp"

using namespace mwmp;

PacketPlayerCooldowns::PacketPlayerCooldowns() : PlayerPacket()
{
    packetID = ID_PLAYER_COOLDOWNS;
}

void PacketPlayerCooldowns::Packet(bool send)
{
    PlayerPacket::Packet(send);

    uint32_t count = 0;

    if (send)
        count = static_cast<uint32_t>(player->cooldownChanges.size());

    if (!CollectionSize(count))
        return;

    if (!send)
    {
        player->cooldownChanges.clear();
        player->cooldownChanges.resize(count);
    }

    for (auto &&spell : player->cooldownChanges)
    {
        Field(spell.id, true);
        Field(spell.startTimestampDay);
        Field(spell.startTimestampHour);
    }
}
