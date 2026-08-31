#include <components/openmw-mp/NetworkMessages.hpp>
#include "PacketPlayerTopic.hpp"

using namespace mwmp;

PacketPlayerTopic::PacketPlayerTopic() : PlayerPacket()
{
    packetID = ID_PLAYER_TOPIC;
}

void PacketPlayerTopic::Packet(bool send)
{
    PlayerPacket::Packet(send);

    uint32_t count = 0;

    if (send)
        count = static_cast<uint32_t>(player->topicChanges.size());

    if (!CollectionSize(count))
        return;

    if (!send)
    {
        player->topicChanges.clear();
        player->topicChanges.resize(count);
    }

    for (auto &&topic : player->topicChanges)
    {
        Field(topic.topicId, true);
    }
}
