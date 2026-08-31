#include <components/openmw-mp/NetworkMessages.hpp>
#include "PacketObjectMiscellaneous.hpp"

using namespace mwmp;

PacketObjectMiscellaneous::PacketObjectMiscellaneous() : ObjectPacket()
{
    packetID = ID_OBJECT_MISCELLANEOUS;
    hasCellData = true;
}

void PacketObjectMiscellaneous::Object(BaseObject &baseObject, bool send)
{
    ObjectPacket::Object(baseObject, send);
    Field(baseObject.goldPool);
    Field(baseObject.lastGoldRestockHour);
    Field(baseObject.lastGoldRestockDay);
}
