#include <components/openmw-mp/NetworkMessages.hpp>
#include "PacketPlayerJournal.hpp"

using namespace mwmp;

PacketPlayerJournal::PacketPlayerJournal() : PlayerPacket()
{
    packetID = ID_PLAYER_JOURNAL;
}

void PacketPlayerJournal::Packet(bool send)
{
    PlayerPacket::Packet(send);

    uint32_t count = 0;

    if (send)
        count = static_cast<uint32_t>(player->journalChanges.size());

    if (!CollectionSize(count))
        return;

    if (!send)
    {
        player->journalChanges.clear();
        player->journalChanges.resize(count);
    }

    for (auto &&journalItem : player->journalChanges)
    {
        Field(journalItem.type);
        Field(journalItem.quest, true);
        Field(journalItem.index);

        if (journalItem.type == JournalItem::ENTRY)
        {
            Field(journalItem.actorRefId, true);

            Field(journalItem.hasTimestamp);

            if (journalItem.hasTimestamp)
            {
                Field(journalItem.timestamp.daysPassed);
                Field(journalItem.timestamp.month);
                Field(journalItem.timestamp.day);
            }
        }
    }
}
