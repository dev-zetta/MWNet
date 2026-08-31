#ifndef OPENMW_PACKETPLAYERITEMUSE_HPP
#define OPENMW_PACKETPLAYERITEMUSE_HPP

#include <components/openmw-mp/Packets/Player/PlayerPacket.hpp>

namespace mwmp
{
    class PacketPlayerItemUse : public PlayerPacket
    {
    public:
        PacketPlayerItemUse();

        virtual void Packet(bool send);
    };
}

#endif //OPENMW_PACKETPLAYERITEMUSE_HPP
