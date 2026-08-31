#ifndef OPENMW_PACKETPLAYERCELLSTATE_HPP
#define OPENMW_PACKETPLAYERCELLSTATE_HPP

#include <components/openmw-mp/Packets/Player/PlayerPacket.hpp>

namespace mwmp
{
    class PacketPlayerCellState : public PlayerPacket
    {
    public:
        PacketPlayerCellState();

        virtual void Packet(bool send);
    };
}

#endif //OPENMW_PACKETPLAYERCELLSTATE_HPP
