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

    RW(player->jailAction, send);
    RW(player->jailSentenceId, send);
    if (!isPacketValid())
        return;

    if (player->jailAction == JailAction::Complete)
        return;
    if (player->jailAction != JailAction::Begin)
    {
        invalidate(protocol::CodecError::InvalidValue);
        return;
    }

    RW(player->jailDays, send);
    RW(player->ignoreJailTeleportation, send);
    RW(player->ignoreJailSkillIncreases, send);
    RW(player->jailProgressText, send, true);
    RW(player->jailEndText, send, true);
}
