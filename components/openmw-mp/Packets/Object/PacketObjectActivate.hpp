#ifndef OPENMW_PACKETOBJECTACTIVATE_HPP
#define OPENMW_PACKETOBJECTACTIVATE_HPP

#include <components/openmw-mp/Packets/Object/ObjectPacket.hpp>

namespace mwmp
{
    class PacketObjectActivate : public ObjectPacket
    {
    public:
        PacketObjectActivate();

        virtual void Packet(bool send);
    };
}

#endif //OPENMW_PACKETOBJECTACTIVATE_HPP
