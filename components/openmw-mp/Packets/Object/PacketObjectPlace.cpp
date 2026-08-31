#include <components/openmw-mp/NetworkMessages.hpp>
#include "PacketObjectPlace.hpp"

using namespace mwmp;

PacketObjectPlace::PacketObjectPlace() : ObjectPacket()
{
    packetID = ID_OBJECT_PLACE;
    hasCellData = true;
}

void PacketObjectPlace::Object(BaseObject &baseObject, bool send)
{
    ObjectPacket::Object(baseObject, send);
    Field(baseObject.count);
    Field(baseObject.charge);
    Field(baseObject.enchantmentCharge);
    Field(baseObject.soul, true);
    Field(baseObject.goldValue);
    Field(baseObject.position);
    Field(baseObject.droppedByPlayer);
    Field(baseObject.hasContainer);
}
