#include <components/openmw-mp/NetworkMessages.hpp>
#include "PacketPlayerCast.hpp"

#include <components/openmw-mp/TimedLog.hpp>

using namespace mwmp;

PacketPlayerCast::PacketPlayerCast() : PlayerPacket()
{
    packetID = ID_PLAYER_CAST;
}

void PacketPlayerCast::Packet(bool send)
{
    PlayerPacket::Packet(send);

    Field(player->cast.target.isPlayer);

    if (player->cast.target.isPlayer)
    {
        Field(player->cast.target.guid);
    }
    else
    {
        Field(player->cast.target.refId, true);
        Field(player->cast.target.refNum);
        Field(player->cast.target.mpNum);
    }

    Field(player->cast.type);

    if (player->cast.type == mwmp::Cast::ITEM)
        Field(player->cast.itemId, true);
    else
    {
        Field(player->cast.pressed);
        Field(player->cast.success);

        Field(player->cast.instant);
        Field(player->cast.spellId, true);
    }

    Field(player->cast.hasProjectile);

    if (player->cast.hasProjectile)
    {
        Field(player->cast.projectileOrigin.origin[0]);
        Field(player->cast.projectileOrigin.origin[1]);
        Field(player->cast.projectileOrigin.origin[2]);
        Field(player->cast.projectileOrigin.orientation[0]);
        Field(player->cast.projectileOrigin.orientation[1]);
        Field(player->cast.projectileOrigin.orientation[2]);
        Field(player->cast.projectileOrigin.orientation[3]);
        Field(player->position);
        Field(player->direction);
    }
}
