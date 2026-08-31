#include <components/openmw-mp/NetworkMessages.hpp>
#include "PacketObjectRotate.hpp"

using namespace mwmp;

PacketObjectRotate::PacketObjectRotate() : ObjectPacket()
{
    packetID = ID_OBJECT_ROTATE;
    hasCellData = true;
}

void PacketObjectRotate::Object(BaseObject &baseObject, bool send)
{
    ObjectPacket::Object(baseObject, send);
    Field(baseObject.position.rot[0]);
    Field(baseObject.position.rot[1]);
    Field(baseObject.position.rot[2]);
}
