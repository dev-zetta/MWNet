#include <components/openmw-mp/NetworkMessages.hpp>
#include "PacketPlayerCooldowns.hpp"

using namespace mwmp;

PacketPlayerCooldowns::PacketPlayerCooldowns() : PlayerPacket()
{
    packetID = ID_PLAYER_COOLDOWNS;
}

void PacketPlayerCooldowns::Packet(RakNet::BitStream *newBitstream, bool send)
{
    PlayerPacket::Packet(newBitstream, send);

    uint32_t count = 0;

    if (send)
        count = static_cast<uint32_t>(player->cooldownChanges.size());

    if (!RWCount(count, send))
        return;

    if (!send)
    {
        player->cooldownChanges.clear();
        player->cooldownChanges.resize(count);
    }

    for (auto &&spell : player->cooldownChanges)
    {
        RW(spell.id, send, true);
        RW(spell.startTimestampDay, send);
        RW(spell.startTimestampHour, send);
    }
}
