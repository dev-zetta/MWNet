#include <components/openmw-mp/NetworkMessages.hpp>
#include "PacketScriptMemberShort.hpp"

using namespace mwmp;

PacketScriptMemberShort::PacketScriptMemberShort() : ObjectPacket()
{
    packetID = ID_SCRIPT_MEMBER_SHORT;
}

void PacketScriptMemberShort::Object(BaseObject &baseObject, bool send)
{
    //Field(baseObject.refId);
    //Field(baseObject.index);
    //Field(baseObject.shortVal);
}
