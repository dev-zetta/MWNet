#ifndef OPENMW_PACKETOBJECTMOVE_HPP
#define OPENMW_PACKETOBJECTMOVE_HPP

#include <components/openmw-mp/Packets/Object/ObjectPacket.hpp>

namespace mwmp
{
    class PacketObjectMove : public ObjectPacket
    {
    public:
        PacketObjectMove();

        virtual void Object(BaseObject &baseObject, bool send);
    };
}

#endif //OPENMW_PACKETOBJECTMOVE_HPP
