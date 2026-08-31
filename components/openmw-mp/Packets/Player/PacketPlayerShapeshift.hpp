#ifndef OPENMW_PACKETPLAYERSHAPESHIFT_HPP
#define OPENMW_PACKETPLAYERSHAPESHIFT_HPP

#include <components/openmw-mp/Packets/Player/PlayerPacket.hpp>

namespace mwmp
{
    class PacketPlayerShapeshift : public PlayerPacket
    {
    public:
        PacketPlayerShapeshift();

        virtual void Packet(bool send);
    };
}

#endif //OPENMW_PACKETPLAYERSHAPESHIFT_HPP
