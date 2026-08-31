#include <components/openmw-mp/NetworkMessages.hpp>
#include "PacketObjectScale.hpp"

using namespace mwmp;

PacketObjectScale::PacketObjectScale() : ObjectPacket()
{
    packetID = ID_OBJECT_SCALE;
    hasCellData = true;
}

void PacketObjectScale::Object(BaseObject &baseObject, bool send)
{
    ObjectPacket::Object(baseObject, send);
    Field(baseObject.scale);
}
