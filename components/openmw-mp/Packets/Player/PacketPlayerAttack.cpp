#include <components/openmw-mp/NetworkMessages.hpp>
#include "PacketPlayerAttack.hpp"

using namespace mwmp;

PacketPlayerAttack::PacketPlayerAttack() : PlayerPacket()
{
    packetID = ID_PLAYER_ATTACK;
}

void PacketPlayerAttack::Packet(bool send)
{
    PlayerPacket::Packet(send);

    Field(player->attack.target.isPlayer);

    if (player->attack.target.isPlayer)
    {
        Field(player->attack.target.guid);
    }
    else
    {
        Field(player->attack.target.refId, true);
        Field(player->attack.target.refNum);
        Field(player->attack.target.mpNum);
    }

    Field(player->attack.type);

    Field(player->attack.pressed);
    Field(player->attack.success);

    Field(player->attack.isHit);

    if (player->attack.type == mwmp::Attack::MELEE)
    {
        Field(player->attack.attackAnimation, true);
    }
    else if (player->attack.type == mwmp::Attack::RANGED)
    {
        Field(player->attack.attackStrength);
        Field(player->attack.rangedWeaponId, true);
        Field(player->attack.rangedAmmoId, true);

        Field(player->attack.projectileOrigin.origin[0]);
        Field(player->attack.projectileOrigin.origin[1]);
        Field(player->attack.projectileOrigin.origin[2]);
        Field(player->attack.projectileOrigin.orientation[0]);
        Field(player->attack.projectileOrigin.orientation[1]);
        Field(player->attack.projectileOrigin.orientation[2]);
        Field(player->attack.projectileOrigin.orientation[3]);
    }

    if (player->attack.isHit)
    {
        Field(player->attack.damage);
        Field(player->attack.block);
        Field(player->attack.knockdown);
        Field(player->attack.applyWeaponEnchantment);

        if (player->attack.type == mwmp::Attack::RANGED)
            Field(player->attack.applyAmmoEnchantment);

        Field(player->attack.hitPosition.pos[0]);
        Field(player->attack.hitPosition.pos[1]);
        Field(player->attack.hitPosition.pos[2]);
    }
}
