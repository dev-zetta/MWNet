#ifndef OPENMW_PACKETPLAYERBASEINFO_HPP
#define OPENMW_PACKETPLAYERBASEINFO_HPP

#include <components/openmw-mp/Packets/Player/PlayerPacket.hpp>

namespace mwmp
{
    class PacketPlayerBaseInfo : public PlayerPacket
    {
    public:
        PacketPlayerBaseInfo();

        virtual void Packet(bool send);
    };
}

#endif //OPENMW_PACKETPLAYERBASEINFO_HPP
