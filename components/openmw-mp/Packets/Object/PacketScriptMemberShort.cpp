#include <components/openmw-mp/NetworkMessages.hpp>
#include "PacketScriptMemberShort.hpp"

using namespace mwmp;

PacketScriptMemberShort::PacketScriptMemberShort() : ObjectPacket()
{
    packetID = ID_SCRIPT_MEMBER_SHORT;
}

void PacketScriptMemberShort::Object(BaseObject &baseObject, bool send)
{
    //RW(baseObject.refId, send);
    //RW(baseObject.index, send);
    //RW(baseObject.shortVal, send);
}
