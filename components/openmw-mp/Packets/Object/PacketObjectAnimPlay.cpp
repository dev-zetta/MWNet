#include <components/openmw-mp/NetworkMessages.hpp>
#include "PacketObjectAnimPlay.hpp"

using namespace mwmp;

PacketObjectAnimPlay::PacketObjectAnimPlay() : ObjectPacket()
{
    packetID = ID_OBJECT_ANIM_PLAY;
    hasCellData = true;
}

void PacketObjectAnimPlay::Object(BaseObject &baseObject, bool send)
{
    ObjectPacket::Object(baseObject, send);
    RW(baseObject.animGroup, send);
    RW(baseObject.animMode, send);
}
