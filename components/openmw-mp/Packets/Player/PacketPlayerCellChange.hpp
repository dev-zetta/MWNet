#ifndef OPENMW_PACKETPLAYERCELLCHANGE_HPP
#define OPENMW_PACKETPLAYERCELLCHANGE_HPP

#include <components/openmw-mp/Packets/Player/PlayerPacket.hpp>

namespace mwmp
{
    class PacketPlayerCellChange : public PlayerPacket
    {
    public:
        PacketPlayerCellChange();

        virtual void Packet(bool send);
    };
}

#endif //OPENMW_PACKETPLAYERCELLCHANGE_HPP
