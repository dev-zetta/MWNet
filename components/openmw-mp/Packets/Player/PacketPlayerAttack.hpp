#ifndef OPENMW_PACKETPLAYERATTACK_HPP
#define OPENMW_PACKETPLAYERATTACK_HPP

#include <components/openmw-mp/Packets/Player/PlayerPacket.hpp>

namespace mwmp
{
    class PacketPlayerAttack : public PlayerPacket
    {
    public:
        PacketPlayerAttack();

        virtual void Packet(bool send);
    };
}

#endif //OPENMW_PACKETPLAYERATTACK_HPP
