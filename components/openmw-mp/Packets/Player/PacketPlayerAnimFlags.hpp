#ifndef OPENMW_PACKETPLAYERANIMFLAGS_HPP
#define OPENMW_PACKETPLAYERANIMFLAGS_HPP

#include <components/openmw-mp/Packets/Player/PlayerPacket.hpp>

namespace mwmp
{
    class PacketPlayerAnimFlags : public PlayerPacket
    {
    public:
        PacketPlayerAnimFlags();

        virtual void Packet(bool send);
    };
}

#endif //OPENMW_PACKETPLAYERANIMFLAGS_HPP
