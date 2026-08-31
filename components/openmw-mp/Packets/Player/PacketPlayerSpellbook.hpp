#ifndef OPENMW_PACKETPLAYERSPELLBOOK_HPP
#define OPENMW_PACKETPLAYERSPELLBOOK_HPP

#include <components/openmw-mp/Packets/Player/PlayerPacket.hpp>

namespace mwmp
{
    class PacketPlayerSpellbook : public PlayerPacket
    {
    public:
        PacketPlayerSpellbook();

        virtual void Packet(RakNet::BitStream *newBitstream, bool send);
    };
}

#endif //OPENMW_PACKETPLAYERSPELLBOOK_HPP
