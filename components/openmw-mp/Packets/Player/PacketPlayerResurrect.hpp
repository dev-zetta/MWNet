#ifndef OPENMW_PACKETPLAYERRESURRECT_HPP
#define OPENMW_PACKETPLAYERRESURRECT_HPP

#include <components/openmw-mp/Packets/Player/PlayerPacket.hpp>

namespace mwmp
{
    class PacketPlayerResurrect : public PlayerPacket
    {
    public:
        PacketPlayerResurrect();

        virtual void Packet(bool send);
    };
}

#endif //OPENMW_PACKETPLAYERRESURRECT_HPP
