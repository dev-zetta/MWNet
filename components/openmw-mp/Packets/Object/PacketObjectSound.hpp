#ifndef OPENMW_PACKETOBJECTSOUND_HPP
#define OPENMW_PACKETOBJECTSOUND_HPP

#include <components/openmw-mp/Packets/Object/ObjectPacket.hpp>

namespace mwmp
{
    class PacketObjectSound : public ObjectPacket
    {
    public:
        PacketObjectSound();

        virtual void Packet(bool send);
    };
}

#endif //OPENMW_PACKETOBJECTSOUND_HPP
