#include <components/openmw-mp/NetworkMessages.hpp>
#include <components/openmw-mp/TimedLog.hpp>
#include "PacketActorEquipment.hpp"

using namespace mwmp;

PacketActorEquipment::PacketActorEquipment() : ActorPacket()
{
    packetID = ID_ACTOR_EQUIPMENT;
}

void PacketActorEquipment::Actor(BaseActor &actor, bool send)
{
    for (auto &&equipmentItem : actor.equipmentItems)
    {
        Field(equipmentItem.refId);
        Field(equipmentItem.count);
        Field(equipmentItem.charge);
        Field(equipmentItem.enchantmentCharge);
    }
}
