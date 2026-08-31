#include <components/openmw-mp/NetworkMessages.hpp>
#include "PacketObjectTrap.hpp"

using namespace mwmp;

PacketObjectTrap::PacketObjectTrap() : ObjectPacket()
{
    packetID = ID_OBJECT_TRAP;
    hasCellData = true;
}

void PacketObjectTrap::Object(BaseObject &baseObject, bool send)
{
    ObjectPacket::Object(baseObject, send);
    Field(baseObject.isDisarmed);

    if (!baseObject.isDisarmed)
        Field(baseObject.position);
}
