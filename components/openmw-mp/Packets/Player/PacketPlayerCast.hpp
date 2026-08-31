#ifndef OPENMW_PACKETPLAYERCAST_HPP
#define OPENMW_PACKETPLAYERCAST_HPP

#include <components/openmw-mp/Packets/Player/PlayerPacket.hpp>

namespace mwmp
{
    class PacketPlayerCast : public PlayerPacket
    {
    public:
        PacketPlayerCast();

        virtual void Packet(bool send);
    };
}

#endif //OPENMW_PACKETPLAYERCAST_HPP
