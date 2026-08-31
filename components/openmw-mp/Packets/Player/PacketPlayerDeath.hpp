#ifndef OPENMW_PACKETPLAYERDEATH_HPP
#define OPENMW_PACKETPLAYERDEATH_HPP

#include <components/openmw-mp/Packets/Player/PlayerPacket.hpp>

namespace mwmp
{
    class PacketPlayerDeath: public PlayerPacket
    {
    public:
        PacketPlayerDeath();

        virtual void Packet(bool send);
    };
}

#endif //OPENMW_PACKETPLAYERDEATH_HPP
