#ifndef OPENMW_PACKETPLAYERBOUNTY_HPP
#define OPENMW_PACKETPLAYERBOUNTY_HPP

#include <components/openmw-mp/Packets/Player/PlayerPacket.hpp>

namespace mwmp
{
    class PacketPlayerBounty : public PlayerPacket
    {
    public:
        PacketPlayerBounty();

        virtual void Packet(bool send);
    };
}

#endif //OPENMW_PACKETPLAYERBOUNTY_HPP
