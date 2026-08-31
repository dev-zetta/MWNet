#ifndef OPENMW_PACKETSCRIPTMEMBERSHORT_HPP
#define OPENMW_PACKETSCRIPTMEMBERSHORT_HPP

#include <components/openmw-mp/Packets/Object/ObjectPacket.hpp>

namespace mwmp
{
    class PacketScriptMemberShort : public ObjectPacket
    {
    public:
        PacketScriptMemberShort();

        virtual void Object(BaseObject &obj, bool send);
    };
}

#endif //OPENMW_PACKETSCRIPTMEMBERSHORT_HPP
