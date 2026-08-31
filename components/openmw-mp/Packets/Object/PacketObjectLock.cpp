#include <components/openmw-mp/NetworkMessages.hpp>
#include "PacketObjectLock.hpp"

using namespace mwmp;

PacketObjectLock::PacketObjectLock() : ObjectPacket()
{
    packetID = ID_OBJECT_LOCK;
    hasCellData = true;
}

void PacketObjectLock::Object(BaseObject &baseObject, bool send)
{
    ObjectPacket::Object(baseObject, send);
    RW(baseObject.lockLevel, send);
}
