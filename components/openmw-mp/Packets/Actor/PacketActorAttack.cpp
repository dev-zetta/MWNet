#include <components/openmw-mp/NetworkMessages.hpp>
#include <components/openmw-mp/TimedLog.hpp>
#include "PacketActorAttack.hpp"

using namespace mwmp;

PacketActorAttack::PacketActorAttack() : ActorPacket()
{
    packetID = ID_ACTOR_ATTACK;
}

void PacketActorAttack::Actor(BaseActor &actor, bool send)
{
    Field(actor.attack.target.isPlayer);

    if (actor.attack.target.isPlayer)
    {
        Field(actor.attack.target.guid);
    }
    else
    {
        Field(actor.attack.target.refId, true);
        Field(actor.attack.target.refNum);
        Field(actor.attack.target.mpNum);
    }

    Field(actor.attack.type);

    Field(actor.attack.pressed);
    Field(actor.attack.success);

    Field(actor.attack.isHit);

    if (actor.attack.type == mwmp::Attack::MELEE)
    {
        Field(actor.attack.attackAnimation, true);
    }
    else if (actor.attack.type == mwmp::Attack::RANGED)
    {
        Field(actor.attack.attackStrength);
        Field(actor.attack.rangedWeaponId, true);
        Field(actor.attack.rangedAmmoId, true);

        Field(actor.attack.projectileOrigin.origin[0]);
        Field(actor.attack.projectileOrigin.origin[1]);
        Field(actor.attack.projectileOrigin.origin[2]);
        Field(actor.attack.projectileOrigin.orientation[0]);
        Field(actor.attack.projectileOrigin.orientation[1]);
        Field(actor.attack.projectileOrigin.orientation[2]);
        Field(actor.attack.projectileOrigin.orientation[3]);
    }

    if (actor.attack.isHit)
    {
        Field(actor.attack.damage);
        Field(actor.attack.block);
        Field(actor.attack.knockdown);
        Field(actor.attack.applyWeaponEnchantment);

        if (actor.attack.type == mwmp::Attack::RANGED)
            Field(actor.attack.applyAmmoEnchantment);

        Field(actor.attack.hitPosition.pos[0]);
        Field(actor.attack.hitPosition.pos[1]);
        Field(actor.attack.hitPosition.pos[2]);
    }
}
