#include "PacketPlayerJail.hpp"
#include <components/openmw-mp/NetworkMessages.hpp>
#include <components/openmw-mp/TimedLog.hpp>

using namespace mwmp;

PacketPlayerJail::PacketPlayerJail() : PlayerPacket()
{
    packetID = ID_PLAYER_JAIL;
}

void PacketPlayerJail::Packet(bool send)
{
    PlayerPacket::Packet(send);

    Field(player->jailAction);
    Field(player->jailSentenceId);
    if (!isPacketValid())
        return;

    if (player->jailAction == JailAction::Complete)
        return;
    if (player->jailAction != JailAction::Begin)
    {
        invalidate(protocol::CodecError::InvalidValue);
        return;
    }

    Field(player->jailDays);
    Field(player->ignoreJailTeleportation);
    Field(player->ignoreJailSkillIncreases);
    Field(player->jailProgressText, true);
    Field(player->jailEndText, true);
}
