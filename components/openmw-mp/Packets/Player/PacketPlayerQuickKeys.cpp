#include <components/openmw-mp/NetworkMessages.hpp>
#include "PacketPlayerQuickKeys.hpp"

using namespace mwmp;

PacketPlayerQuickKeys::PacketPlayerQuickKeys() : PlayerPacket()
{
    packetID = ID_PLAYER_QUICKKEYS;
}

void PacketPlayerQuickKeys::Packet(bool send)
{
    PlayerPacket::Packet(send);

    uint32_t count = 0;

    if (send)
        count = static_cast<uint32_t>(player->quickKeyChanges.size());

    if (!CollectionSize(count))
        return;

    if (!send)
    {
        player->quickKeyChanges.clear();
        player->quickKeyChanges.resize(count);
    }

    for (auto &&quickKey : player->quickKeyChanges)
    {
        Field(quickKey.type);
        Field(quickKey.slot);

        if (quickKey.type != QuickKey::UNASSIGNED)
            Field(quickKey.itemId);
    }
}
