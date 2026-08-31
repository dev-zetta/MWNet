#include <components/openmw-mp/NetworkMessages.hpp>
#include "PacketPlayerSpellbook.hpp"

using namespace mwmp;

PacketPlayerSpellbook::PacketPlayerSpellbook() : PlayerPacket()
{
    packetID = ID_PLAYER_SPELLBOOK;
}

void PacketPlayerSpellbook::Packet(bool send)
{
    PlayerPacket::Packet(send);

    Field(player->spellbookChanges.action);

    uint32_t count = 0;

    if (send)
        count = static_cast<uint32_t>(player->spellbookChanges.spells.size());

    if (!CollectionSize(count))
        return;

    if (!send)
    {
        player->spellbookChanges.spells.clear();
        player->spellbookChanges.spells.resize(count);
    }

    for (auto &&spell : player->spellbookChanges.spells)
    {
        Field(spell.mId, true);
    }
}
